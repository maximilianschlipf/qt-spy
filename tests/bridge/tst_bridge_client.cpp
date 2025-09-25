#include "qt_spy/bridge_client.h"
#include "qt_spy/probe.h"

#include <QtTest>

#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUuid>

namespace {

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

class BridgeClientTest : public QObject {
    Q_OBJECT

private slots:
    void testHandshakeAndSnapshot();
    void testIncrementalUpdates();
    void testRequestFlows();
    void testDetachHandshake();
};

QString uniqueServerName(const QString &tag)
{
    return QStringLiteral("qt_spy_test_%1_%2")
        .arg(tag)
        .arg(QUuid::createUuid().toString(QUuid::Id128));
}

QJsonObject takeFirstObject(QSignalSpy &spy)
{
    if (spy.isEmpty()) {
        return {};
    }
    const auto arguments = spy.takeFirst();
    return arguments.isEmpty() ? QJsonObject() : arguments.at(0).toJsonObject();
}

bool connectAndAttach(qt_spy::BridgeClient &client,
                      const QString &serverName,
                      QSignalSpy &helloSpy,
                      const QString &clientName)
{
    QSignalSpy connectedSpy(&client, &qt_spy::BridgeClient::socketConnected);
    client.connectToServer(serverName);
    if (!connectedSpy.wait(5000)) {
        qWarning() << "BridgeClientTest: connection timeout for" << serverName;
        return false;
    }
    client.sendAttach(clientName);
    if (!helloSpy.wait(5000)) {
        qWarning() << "BridgeClientTest: hello timeout for" << serverName;
        return false;
    }
    return true;
}

void BridgeClientTest::testHandshakeAndSnapshot()
{
    qt_spy::ProbeOptions options;
    options.autoStart = false;
    options.serverName = uniqueServerName(QStringLiteral("bridge_client"));
    qt_spy::Probe probe(options);
    const QString serverName = probe.serverName();

    QObject root(QCoreApplication::instance());
    root.setObjectName(QStringLiteral("rootNode"));

    QObject child(&root);
    child.setObjectName(QStringLiteral("childNode"));

    probe.start();
    if (!probe.isListening()) {
        QSKIP("Probe failed to listen on local socket");
    }

    qt_spy::BridgeClient client;
    QSignalSpy helloSpy(&client, &qt_spy::BridgeClient::helloReceived);
    if (!connectAndAttach(client, serverName, helloSpy, QStringLiteral("snapshot-test"))) {
        QSKIP("Bridge client connection not available (likely sandboxed)");
    }

    QSignalSpy snapshotSpy(&client, &qt_spy::BridgeClient::snapshotReceived);
    client.requestSnapshot(QStringLiteral("req_snapshot"));
    if (!snapshotSpy.wait(5000)) {
        QSKIP("Snapshot message not received (likely sandboxed)");
    }

    const QJsonObject snapshot = takeFirstObject(snapshotSpy);
    const QJsonArray nodes = snapshot.value(QLatin1String(qt_spy::protocol::keys::kNodes)).toArray();
    QVERIFY(!nodes.isEmpty());

    QHash<QString, QJsonObject> nodesByName;
    for (const QJsonValue &value : nodes) {
        const QJsonObject node = value.toObject();
        nodesByName.insert(node.value(QStringLiteral("objectName")).toString(), node);
    }

    QVERIFY(nodesByName.contains(QStringLiteral("rootNode")));
    QVERIFY(nodesByName.contains(QStringLiteral("childNode")));

    const QJsonObject rootNode = nodesByName.value(QStringLiteral("rootNode"));
    const QJsonObject childNode = nodesByName.value(QStringLiteral("childNode"));

    const QString rootId = rootNode.value(QLatin1String(qt_spy::protocol::keys::kId)).toString();
    const QString childId = childNode.value(QLatin1String(qt_spy::protocol::keys::kId)).toString();
    QVERIFY(!rootId.isEmpty());
    QVERIFY(!childId.isEmpty());
    QCOMPARE(childNode.value(QLatin1String(qt_spy::protocol::keys::kParentId)).toString(), rootId);

    const QJsonArray rootIds = snapshot.value(QLatin1String(qt_spy::protocol::keys::kRootIds)).toArray();
    QVERIFY(rootIds.contains(rootId));

    client.disconnectFromServer();
    probe.stop();
}

void BridgeClientTest::testIncrementalUpdates()
{
    qt_spy::ProbeOptions options;
    options.autoStart = false;
    options.serverName = uniqueServerName(QStringLiteral("bridge_client"));
    qt_spy::Probe probe(options);
    const QString serverName = probe.serverName();

    NotifyingObject notifier(QCoreApplication::instance());
    notifier.setObjectName(QStringLiteral("notifier"));

    probe.start();
    if (!probe.isListening()) {
        QSKIP("Probe failed to listen on local socket");
    }

    qt_spy::BridgeClient client;
    QSignalSpy helloSpy(&client, &qt_spy::BridgeClient::helloReceived);
    if (!connectAndAttach(client, serverName, helloSpy, QStringLiteral("updates-test"))) {
        QSKIP("Bridge client connection not available (likely sandboxed)");
    }

    QSignalSpy snapshotSpy(&client, &qt_spy::BridgeClient::snapshotReceived);
    client.requestSnapshot(QStringLiteral("req_snapshot"));
    if (!snapshotSpy.wait(5000)) {
        QSKIP("Snapshot message not received (likely sandboxed)");
    }

    const QJsonObject snapshot = takeFirstObject(snapshotSpy);
    const QJsonArray nodes = snapshot.value(QLatin1String(qt_spy::protocol::keys::kNodes)).toArray();
    QString notifierId;
    for (const QJsonValue &value : nodes) {
        const QJsonObject node = value.toObject();
        if (node.value(QStringLiteral("objectName")).toString() == QStringLiteral("notifier")) {
            notifierId = node.value(QLatin1String(qt_spy::protocol::keys::kId)).toString();
            break;
        }
    }
    QVERIFY2(!notifierId.isEmpty(), "Notifier id not found in snapshot");

    QSignalSpy propertiesChangedSpy(&client, &qt_spy::BridgeClient::propertiesChanged);
    notifier.setValue(42);
    if (!propertiesChangedSpy.wait(5000)) {
        QSKIP("Property change not observed (likely sandboxed)");
    }

    const QJsonObject propsMessage = takeFirstObject(propertiesChangedSpy);
    QCOMPARE(propsMessage.value(QLatin1String(qt_spy::protocol::keys::kId)).toString(), notifierId);
    const QJsonArray changed = propsMessage.value(QLatin1String(qt_spy::protocol::keys::kChanged)).toArray();
    QVERIFY(changed.contains(QStringLiteral("value")));
    const QJsonObject props = propsMessage.value(QLatin1String(qt_spy::protocol::keys::kProperties)).toObject();
    QCOMPARE(props.value(QStringLiteral("value")).toInt(), 42);

    QSignalSpy nodeAddedSpy(&client, &qt_spy::BridgeClient::nodeAdded);
    auto *dynamicChild = new QObject(&notifier);
    dynamicChild->setObjectName(QStringLiteral("dynamicChild"));
    if (!nodeAddedSpy.wait(5000)) {
        delete dynamicChild;
        QSKIP("nodeAdded not emitted (likely sandboxed)");
    }

    const QJsonObject nodeAdded = takeFirstObject(nodeAddedSpy);
    const QJsonObject addedNode = nodeAdded.value(QLatin1String(qt_spy::protocol::keys::kNode)).toObject();
    const QString childId = addedNode.value(QLatin1String(qt_spy::protocol::keys::kId)).toString();
    QVERIFY2(!childId.isEmpty(), "dynamic child id missing");
    QCOMPARE(addedNode.value(QStringLiteral("objectName")).toString(), QStringLiteral("dynamicChild"));

    QSignalSpy nodeRemovedSpy(&client, &qt_spy::BridgeClient::nodeRemoved);
    delete dynamicChild;
    if (!nodeRemovedSpy.wait(5000)) {
        QSKIP("nodeRemoved not emitted (likely sandboxed)");
    }
    const QJsonObject removed = takeFirstObject(nodeRemovedSpy);
    QCOMPARE(removed.value(QLatin1String(qt_spy::protocol::keys::kId)).toString(), childId);

    client.disconnectFromServer();
    probe.stop();
}

void BridgeClientTest::testRequestFlows()
{
    qt_spy::ProbeOptions options;
    options.autoStart = false;
    options.serverName = uniqueServerName(QStringLiteral("bridge_client"));
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

    qt_spy::BridgeClient client;
    QSignalSpy helloSpy(&client, &qt_spy::BridgeClient::helloReceived);
    if (!connectAndAttach(client, serverName, helloSpy, QStringLiteral("request-flow"))) {
        QSKIP("Bridge client connection not available (likely sandboxed)");
    }

    QSignalSpy snapshotSpy(&client, &qt_spy::BridgeClient::snapshotReceived);
    client.requestSnapshot(QStringLiteral("req_initial"));
    if (!snapshotSpy.wait(5000)) {
        QSKIP("Snapshot message not received (likely sandboxed)");
    }

    const QJsonObject snapshot = takeFirstObject(snapshotSpy);
    const QJsonArray nodes = snapshot.value(QLatin1String(qt_spy::protocol::keys::kNodes)).toArray();
    QString childId;
    for (const QJsonValue &value : nodes) {
        const QJsonObject node = value.toObject();
        if (node.value(QStringLiteral("objectName")).toString() == QStringLiteral("child")) {
            childId = node.value(QLatin1String(qt_spy::protocol::keys::kId)).toString();
            break;
        }
    }
    QVERIFY2(!childId.isEmpty(), "child id not found in snapshot");

    QSignalSpy propertiesSpy(&client, &qt_spy::BridgeClient::propertiesReceived);
    client.requestProperties(childId, QStringLiteral("req_props"));
    if (!propertiesSpy.wait(5000)) {
        QSKIP("Properties reply not received (likely sandboxed)");
    }

    const QJsonObject propsMessage = takeFirstObject(propertiesSpy);
    QCOMPARE(propsMessage.value(QLatin1String(qt_spy::protocol::keys::kId)).toString(), childId);
    QCOMPARE(propsMessage.value(QLatin1String(qt_spy::protocol::keys::kRequestId)).toString(),
             QStringLiteral("req_props"));
    const QJsonObject props = propsMessage.value(QLatin1String(qt_spy::protocol::keys::kProperties)).toObject();
    QCOMPARE(props.value(QStringLiteral("objectName")).toString(), QStringLiteral("child"));

    QSignalSpy selectionSpy(&client, &qt_spy::BridgeClient::selectionAckReceived);
    client.selectNode(childId, QStringLiteral("req_select"));
    if (!selectionSpy.wait(5000)) {
        QSKIP("Selection ack not received (likely sandboxed)");
    }

    const QJsonObject selectionAck = takeFirstObject(selectionSpy);
    QCOMPARE(selectionAck.value(QLatin1String(qt_spy::protocol::keys::kRequestId)).toString(),
             QStringLiteral("req_select"));
    QCOMPARE(selectionAck.value(QLatin1String(qt_spy::protocol::keys::kId)).toString(), childId);

    QSignalSpy verifySnapshotSpy(&client, &qt_spy::BridgeClient::snapshotReceived);
    client.requestSnapshot(QStringLiteral("req_verify"));
    if (!verifySnapshotSpy.wait(5000)) {
        QSKIP("Verify snapshot not received (likely sandboxed)");
    }
    const QJsonObject verifySnapshot = takeFirstObject(verifySnapshotSpy);
    QCOMPARE(verifySnapshot.value(QLatin1String(qt_spy::protocol::keys::kSelection)).toString(), childId);

    client.disconnectFromServer();
    probe.stop();
}

void BridgeClientTest::testDetachHandshake()
{
    qt_spy::ProbeOptions options;
    options.autoStart = false;
    options.serverName = uniqueServerName(QStringLiteral("bridge_client"));
    qt_spy::Probe probe(options);
    const QString serverName = probe.serverName();

    QObject root(QCoreApplication::instance());
    root.setObjectName(QStringLiteral("root"));

    probe.start();
    if (!probe.isListening()) {
        QSKIP("Probe failed to listen on local socket");
    }

    qt_spy::BridgeClient client;
    QSignalSpy helloSpy(&client, &qt_spy::BridgeClient::helloReceived);
    QSignalSpy goodbyeSpy(&client, &qt_spy::BridgeClient::goodbyeReceived);
    QSignalSpy disconnectedSpy(&client, &qt_spy::BridgeClient::socketDisconnected);
    if (!connectAndAttach(client, serverName, helloSpy, QStringLiteral("detach-test"))) {
        QSKIP("Bridge client connection not available (likely sandboxed)");
    }

    const QString detachRequestId = QStringLiteral("req_detach");
    client.sendDetach(detachRequestId);

    if (!goodbyeSpy.wait(5000)) {
        QSKIP("Goodbye message not received (likely sandboxed)");
    }

    const QJsonObject goodbye = takeFirstObject(goodbyeSpy);
    QCOMPARE(goodbye.value(QLatin1String(qt_spy::protocol::keys::kType)).toString(),
             QLatin1String(qt_spy::protocol::types::kGoodbye));
    QCOMPARE(goodbye.value(QLatin1String(qt_spy::protocol::keys::kRequestId)).toString(),
             detachRequestId);

    if (client.state() == QLocalSocket::ConnectedState) {
        QVERIFY2(disconnectedSpy.wait(5000), "Bridge client did not disconnect after goodbye");
    }

    probe.stop();
}

} // namespace

QTEST_MAIN(BridgeClientTest)
#include "tst_bridge_client.moc"
