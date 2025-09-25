#include "qt_spy/probe.h"
#include "qt_spy/protocol.h"

#include <QtTest>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalSocket>
#include <QSet>
#include <QUuid>

#include <QtEndian>

namespace {

namespace protocol = qt_spy::protocol;

QString uniqueServerName(const QString &tag)
{
    return QStringLiteral("qt_spy_test_%1_%2")
        .arg(tag)
        .arg(QUuid::createUuid().toString(QUuid::Id128));
}

class NotifyingObject : public QObject {
    Q_OBJECT
    Q_PROPERTY(int value READ value WRITE setValue NOTIFY valueChanged)
public:
    explicit NotifyingObject(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    int value() const { return m_value; }

    void setValue(int value)
    {
        if (m_value == value) {
            return;
        }
        m_value = value;
        emit valueChanged();
    }

signals:
    void valueChanged();

private:
    int m_value = 0;
};

class ProbeBridgeTest : public QObject {
    Q_OBJECT

private slots:
    void testSnapshotSerialization();
    void testIncrementalUpdates();
    void testRequestFlows();

private:
    static void writeMessage(QLocalSocket &socket, const QJsonObject &message);
    static bool readMessage(QLocalSocket &socket,
                            QByteArray &buffer,
                            QJsonObject *out,
                            int timeoutMs = 2000);
    static bool waitForType(QLocalSocket &socket,
                            QByteArray &buffer,
                            const QString &type,
                            QJsonObject *out,
                            int timeoutMs = 2000);
};

void ProbeBridgeTest::writeMessage(QLocalSocket &socket, const QJsonObject &message)
{
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray frame;
    frame.resize(4);
    qToBigEndian(static_cast<quint32>(payload.size()), reinterpret_cast<uchar *>(frame.data()));
    frame.append(payload);
    socket.write(frame);
    socket.flush();
    socket.waitForBytesWritten(1000);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

bool ProbeBridgeTest::readMessage(QLocalSocket &socket,
                                  QByteArray &buffer,
                                  QJsonObject *out,
                                  int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

        if (socket.bytesAvailable() > 0) {
            buffer += socket.readAll();
        }

        if (buffer.size() >= 4) {
            const quint32 length = qFromBigEndian<quint32>(
                reinterpret_cast<const uchar *>(buffer.constData()));
            if (buffer.size() >= static_cast<int>(length) + 4) {
                const QByteArray payload = buffer.mid(4, static_cast<int>(length));
                buffer.remove(0, static_cast<int>(length) + 4);

                QJsonParseError parseError{};
                const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
                if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                    qWarning() << "readMessage: failed to parse JSON" << parseError.errorString();
                    return false;
                }
                if (out) {
                    *out = doc.object();
                }
                return true;
            }
        }

        QTest::qWait(1);
    }

    qWarning() << "readMessage: timed out waiting for message of size" << buffer.size();
    return false;
}

bool ProbeBridgeTest::waitForType(QLocalSocket &socket,
                                  QByteArray &buffer,
                                  const QString &type,
                                  QJsonObject *out,
                                  int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        QJsonObject message;
        if (!readMessage(socket, buffer, &message, remaining > 0 ? remaining : 0)) {
            continue;
        }
        if (message.value(QLatin1String(protocol::keys::kType)).toString() == type) {
            if (out) {
                *out = message;
            }
            return true;
        }
    }
    return false;
}

void ProbeBridgeTest::testSnapshotSerialization()
{
    qt_spy::ProbeOptions options;
    options.autoStart = false;
    options.serverName = uniqueServerName(QStringLiteral("probe_bridge"));

    qt_spy::Probe probe(options);
    const QString serverName = probe.serverName();

    QObject root(QCoreApplication::instance());
    root.setObjectName(QStringLiteral("rootNode"));
    root.setProperty("dynamicKey", QStringLiteral("dynamicValue"));

    QObject child(&root);
    child.setObjectName(QStringLiteral("childNode"));

    probe.start();
    if (!probe.isListening()) {
        QSKIP("Probe failed to listen on local socket");
    }

    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (!socket.waitForConnected(2000)) {
        QSKIP("Failed to connect to probe server (likely sandboxed)");
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QByteArray buffer;
    QJsonObject message;

    QJsonObject attach;
    attach[QLatin1String(protocol::keys::kType)] = QLatin1String(protocol::types::kAttach);
    attach[QLatin1String(protocol::keys::kProtocolVersion)] = protocol::kVersion;
    attach[QStringLiteral("clientName")] = QStringLiteral("snapshot-test");
    writeMessage(socket, attach);

    QVERIFY(readMessage(socket, buffer, &message, 5000));
    QCOMPARE(message.value(QLatin1String(protocol::keys::kType)).toString(),
             QLatin1String(protocol::types::kHello));

    QJsonObject request;
    request[QLatin1String(protocol::keys::kType)] =
        QLatin1String(protocol::types::kSnapshotRequest);
    request[QLatin1String(protocol::keys::kRequestId)] = QStringLiteral("req_snapshot");
    writeMessage(socket, request);

    QVERIFY(readMessage(socket, buffer, &message, 5000));
    QCOMPARE(message.value(QLatin1String(protocol::keys::kType)).toString(),
             QLatin1String(protocol::types::kSnapshot));

    const QJsonArray nodes = message.value(QLatin1String(protocol::keys::kNodes)).toArray();
    QVERIFY(!nodes.isEmpty());

    QHash<QString, QJsonObject> nodesByName;
    for (const QJsonValue &value : nodes) {
        const QJsonObject node = value.toObject();
        const QString id = node.value(QLatin1String(protocol::keys::kId)).toString();
        QVERIFY(!id.isEmpty());
        nodesByName.insert(node.value(QStringLiteral("objectName")).toString(), node);
    }

    QVERIFY(nodesByName.contains(QStringLiteral("rootNode")));
    QVERIFY(nodesByName.contains(QStringLiteral("childNode")));

    const QJsonObject rootNode = nodesByName.value(QStringLiteral("rootNode"));
    const QJsonObject childNode = nodesByName.value(QStringLiteral("childNode"));

    const QString rootId = rootNode.value(QLatin1String(protocol::keys::kId)).toString();
    const QString childId = childNode.value(QLatin1String(protocol::keys::kId)).toString();
    QVERIFY(!rootId.isEmpty());
    QVERIFY(!childId.isEmpty());

    QVERIFY(!rootNode.contains(QLatin1String(protocol::keys::kParentId)));
    QCOMPARE(childNode.value(QLatin1String(protocol::keys::kParentId)).toString(), rootId);

    const QJsonArray childIds = rootNode.value(QLatin1String(protocol::keys::kChildIds)).toArray();
    QVERIFY(childIds.contains(QJsonValue(childId)));

    const QJsonObject props = rootNode.value(QLatin1String(protocol::keys::kProperties)).toObject();
    const QString dynamicValue = props.value(QStringLiteral("__dynamic")).toObject().value(QStringLiteral("dynamicKey")).toString();
    QCOMPARE(dynamicValue, QStringLiteral("dynamicValue"));

    const QJsonArray rootIds = message.value(QLatin1String(protocol::keys::kRootIds)).toArray();
    QVERIFY(rootIds.contains(QJsonValue(rootId)));

    socket.disconnectFromServer();
    probe.stop();
}

void ProbeBridgeTest::testIncrementalUpdates()
{
    qt_spy::ProbeOptions options;
    options.autoStart = false;
    options.serverName = uniqueServerName(QStringLiteral("probe_bridge"));

    qt_spy::Probe probe(options);
    const QString serverName = probe.serverName();

    NotifyingObject notifier(QCoreApplication::instance());
    notifier.setObjectName(QStringLiteral("notifier"));

    probe.start();
    if (!probe.isListening()) {
        QSKIP("Probe failed to listen on local socket");
    }

    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (!socket.waitForConnected(2000)) {
        QSKIP("Failed to connect to probe server (likely sandboxed)");
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QByteArray buffer;
    QJsonObject message;

    QJsonObject attach;
    attach[QLatin1String(protocol::keys::kType)] = QLatin1String(protocol::types::kAttach);
    attach[QLatin1String(protocol::keys::kProtocolVersion)] = protocol::kVersion;
    attach[QStringLiteral("clientName")] = QStringLiteral("updates-test");
    writeMessage(socket, attach);

    QVERIFY(readMessage(socket, buffer, &message, 5000));
    QCOMPARE(message.value(QLatin1String(protocol::keys::kType)).toString(),
             QLatin1String(protocol::types::kHello));

    QJsonObject request;
    request[QLatin1String(protocol::keys::kType)] =
        QLatin1String(protocol::types::kSnapshotRequest);
    request[QLatin1String(protocol::keys::kRequestId)] = QStringLiteral("req_initial_snapshot");
    writeMessage(socket, request);
    QVERIFY(readMessage(socket, buffer, &message, 5000));
    QCOMPARE(message.value(QLatin1String(protocol::keys::kType)).toString(),
             QLatin1String(protocol::types::kSnapshot));

    notifier.setValue(42);
    QJsonObject propertiesMessage;
    QVERIFY(waitForType(socket, buffer, QLatin1String(protocol::types::kPropertiesChanged),
                        &propertiesMessage));
    const QJsonArray changedProps =
        propertiesMessage.value(QLatin1String(protocol::keys::kChanged)).toArray();
    QVERIFY(changedProps.contains(QStringLiteral("value")));
    const QJsonObject props =
        propertiesMessage.value(QLatin1String(protocol::keys::kProperties)).toObject();
    QCOMPARE(props.value(QStringLiteral("value")).toInt(), 42);

    auto *dynamicChild = new QObject(&notifier);
    dynamicChild->setObjectName(QStringLiteral("dynamicChild"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QJsonObject nodeAdded;
    QVERIFY(waitForType(socket, buffer, QLatin1String(protocol::types::kNodeAdded), &nodeAdded));
    const QJsonObject node = nodeAdded.value(QLatin1String(protocol::keys::kNode)).toObject();
    const QString childId = node.value(QLatin1String(protocol::keys::kId)).toString();
    QVERIFY2(!childId.isEmpty(), "Did not receive nodeAdded with an id");

    QJsonObject childProps;
    for (int attempt = 0; attempt < 5; ++attempt) {
        QVERIFY(waitForType(socket, buffer, QLatin1String(protocol::types::kPropertiesChanged), &childProps));
        if (childProps.value(QLatin1String(protocol::keys::kId)).toString() == childId) {
            break;
        }
    }
    QCOMPARE(childProps.value(QLatin1String(protocol::keys::kId)).toString(), childId);
    const QJsonObject childPropsPayload =
        childProps.value(QLatin1String(protocol::keys::kProperties)).toObject();
    QCOMPARE(childPropsPayload.value(QStringLiteral("objectName")).toString(),
             QStringLiteral("dynamicChild"));

    delete dynamicChild;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QJsonObject nodeRemoved;
    QVERIFY(waitForType(socket, buffer, QLatin1String(protocol::types::kNodeRemoved), &nodeRemoved));
    QCOMPARE(nodeRemoved.value(QLatin1String(protocol::keys::kId)).toString(), childId);

    socket.disconnectFromServer();
    probe.stop();
}

void ProbeBridgeTest::testRequestFlows()
{
    qt_spy::ProbeOptions options;
    options.autoStart = false;
    options.serverName = uniqueServerName(QStringLiteral("probe_bridge"));

    qt_spy::Probe probe(options);
    const QString serverName = probe.serverName();

    QObject root(QCoreApplication::instance());
    root.setObjectName(QStringLiteral("root"));

    QObject child(&root);
    child.setObjectName(QStringLiteral("child"));

    probe.start();
    if (!probe.isListening()) {
        QSKIP("Probe failed to listen on local socket");
    }

    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (!socket.waitForConnected(2000)) {
        QSKIP("Failed to connect to probe server (likely sandboxed)");
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QByteArray buffer;
    QJsonObject message;

    QJsonObject attach;
    attach[QLatin1String(protocol::keys::kType)] = QLatin1String(protocol::types::kAttach);
    attach[QLatin1String(protocol::keys::kProtocolVersion)] = protocol::kVersion;
    attach[QLatin1String(protocol::keys::kClientName)] = QStringLiteral("request-flow-test");
    writeMessage(socket, attach);

    QVERIFY(readMessage(socket, buffer, &message, 5000));
    QCOMPARE(message.value(QLatin1String(protocol::keys::kType)).toString(),
             QLatin1String(protocol::types::kHello));

    QJsonObject snapshotRequest;
    snapshotRequest[QLatin1String(protocol::keys::kType)] =
        QLatin1String(protocol::types::kSnapshotRequest);
    snapshotRequest[QLatin1String(protocol::keys::kRequestId)] = QStringLiteral("req_initial");
    writeMessage(socket, snapshotRequest);

    QVERIFY(readMessage(socket, buffer, &message, 5000));
    QCOMPARE(message.value(QLatin1String(protocol::keys::kType)).toString(),
             QLatin1String(protocol::types::kSnapshot));

    const QJsonArray nodes = message.value(QLatin1String(protocol::keys::kNodes)).toArray();
    QHash<QString, QString> idsByName;
    for (const QJsonValue &value : nodes) {
        const QJsonObject node = value.toObject();
        idsByName.insert(node.value(QStringLiteral("objectName")).toString(),
                         node.value(QLatin1String(protocol::keys::kId)).toString());
    }

    const QString targetId = idsByName.value(QStringLiteral("child"));
    QVERIFY2(!targetId.isEmpty(), "Expected child node id");

    QJsonObject propertiesRequest;
    propertiesRequest[QLatin1String(protocol::keys::kType)] =
        QLatin1String(protocol::types::kPropertiesRequest);
    propertiesRequest[QLatin1String(protocol::keys::kId)] = targetId;
    propertiesRequest[QLatin1String(protocol::keys::kRequestId)] = QStringLiteral("req_props");
    writeMessage(socket, propertiesRequest);

    QJsonObject propertiesMessage;
    QVERIFY(waitForType(socket, buffer, QLatin1String(protocol::types::kProperties),
                        &propertiesMessage));
    QCOMPARE(propertiesMessage.value(QLatin1String(protocol::keys::kId)).toString(), targetId);
    QCOMPARE(propertiesMessage.value(QLatin1String(protocol::keys::kRequestId)).toString(),
             QStringLiteral("req_props"));

    const QJsonObject propertiesPayload =
        propertiesMessage.value(QLatin1String(protocol::keys::kProperties)).toObject();
    QCOMPARE(propertiesPayload.value(QStringLiteral("objectName")).toString(),
             QStringLiteral("child"));

    QJsonObject selectRequest;
    selectRequest[QLatin1String(protocol::keys::kType)] =
        QLatin1String(protocol::types::kSelectNode);
    selectRequest[QLatin1String(protocol::keys::kId)] = targetId;
    selectRequest[QLatin1String(protocol::keys::kRequestId)] = QStringLiteral("req_select");
    writeMessage(socket, selectRequest);

    QJsonObject selectionAck;
    QVERIFY(waitForType(socket, buffer, QLatin1String(protocol::types::kSelectionAck), &selectionAck));
    QCOMPARE(selectionAck.value(QLatin1String(protocol::keys::kId)).toString(), targetId);
    QCOMPARE(selectionAck.value(QLatin1String(protocol::keys::kRequestId)).toString(),
             QStringLiteral("req_select"));

    QJsonObject verifySnapshotRequest;
    verifySnapshotRequest[QLatin1String(protocol::keys::kType)] =
        QLatin1String(protocol::types::kSnapshotRequest);
    verifySnapshotRequest[QLatin1String(protocol::keys::kRequestId)] = QStringLiteral("req_verify");
    writeMessage(socket, verifySnapshotRequest);

    QJsonObject verifySnapshot;
    QVERIFY(waitForType(socket, buffer, QLatin1String(protocol::types::kSnapshot), &verifySnapshot));
    QCOMPARE(verifySnapshot.value(QLatin1String(protocol::keys::kSelection)).toString(), targetId);

    socket.disconnectFromServer();
    probe.stop();
}

} // namespace

QTEST_MAIN(ProbeBridgeTest)

#include "tst_probe_bridge.moc"
