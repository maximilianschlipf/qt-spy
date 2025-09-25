#include "qt_spy/probe.h"
#include "qt_spy/protocol.h"

#include <QApplication>
#include <QByteArray>
#include <QChildEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDynamicPropertyChangeEvent>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QPair>
#include <QPointer>
#include <QRect>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QVariant>
#include <QWidget>
#include <QWindow>

#include <QFile>
#include <QDebug>
#include <QtEndian>
#include <QtGlobal>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace {

QString makeNodeId(const QObject *object)
{
    if (!object) {
        return {};
    }
    return QStringLiteral("node_%1").arg(QString::number(quintptr(object), 16));
}

QJsonValue variantToJson(const QVariant &value)
{
    if (!value.isValid()) {
        return QJsonValue();
    }

    const QJsonValue converted = QJsonValue::fromVariant(value);
    if (converted.isNull() && !value.isNull()) {
        return QJsonValue(value.toString());
    }
    return converted;
}

QJsonObject geometryToJson(const QRect &rect)
{
    QJsonObject geometry;
    geometry["x"] = rect.x();
    geometry["y"] = rect.y();
    geometry["width"] = rect.width();
    geometry["height"] = rect.height();
    return geometry;
}

QJsonObject widgetInfo(const QWidget *widget)
{
    QJsonObject info;
    info["visible"] = widget->isVisible();
    info["enabled"] = widget->isEnabled();
    info["windowTitle"] = widget->windowTitle();
    info["geometry"] = geometryToJson(widget->geometry());
    return info;
}

QJsonObject windowInfo(const QWindow *window)
{
    QJsonObject info;
    info["visible"] = window->isVisible();
    info["title"] = window->title();
    info["geometry"] = geometryToJson(window->geometry());
    return info;
}

QString sanitizeProcessName(const QString &name)
{
    QString sanitized = name;
    sanitized.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    sanitized.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    return sanitized.trimmed();
}

} // namespace

namespace qt_spy {

class ProbeConnection : public QObject {
    Q_OBJECT
public:
    ProbeConnection(QLocalSocket *socket, Probe *probe);
    ~ProbeConnection() override;

    void close();

signals:
    void closed(ProbeConnection *connection);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onReadyRead();
    void onDisconnected();
    void handlePropertyNotify();
    void onObjectDestroyed(QObject *object);

private:
    void processBuffer();
    void handleMessage(const QJsonObject &message);
    void handleAttach(const QJsonObject &message);
    void handleDetach(const QJsonObject &message);
    void handleSnapshotRequest(const QJsonObject &message);
    void handlePropertiesRequest(const QJsonObject &message);
    void handleSelectNode(const QJsonObject &message);

    void sendMessage(const QJsonObject &message);
    void sendError(const QString &code, const QString &text, const QJsonObject &context = {});
    void sendHello();

    QJsonObject buildSnapshotPayload();
    QJsonObject serializeNode(QObject *object, const QString &parentId);
    QJsonObject serializeProperties(QObject *object) const;

    QString ensureIdForObject(const QObject *object);

    void observeProperties(QObject *object);
    void unobserveProperties(QObject *object);

    void installRecursive(QObject *object, const QString &parentId, bool announce);
    void removeRecursive(QObject *object, bool emitEvent);

    void emitNodeAdded(QObject *object, const QString &parentId);
    void emitNodeRemoved(const QString &id, const QString &parentId);
    void emitPropertiesChanged(QObject *object, const QStringList &names);

    QVector<QObject *> ensureRootsTracked(bool announce);
    void refreshTopLevelObjects();
    void cleanup();

    bool m_handshakeComplete = false;
    QLocalSocket *m_socket = nullptr;
    Probe *m_probe = nullptr;
    QByteArray m_readBuffer;
    QTimer m_topLevelPoll;

    QHash<const QObject *, QString> m_idsByObject;
    QHash<QString, QPointer<QObject>> m_objectById;
    QHash<const QObject *, QString> m_parentByObject;
    QHash<const QObject *, QVector<QMetaObject::Connection>> m_propertyConnections;
    QHash<QPair<const QObject *, int>, QString> m_propertyBySignalIndex;
    QSet<const QObject *> m_tracked;
    QString m_selectedId;
};

ProbeConnection::ProbeConnection(QLocalSocket *socket, Probe *probe)
    : QObject(probe)
    , m_socket(socket)
    , m_probe(probe)
    , m_topLevelPoll(this)
{
    Q_ASSERT(m_socket);
    m_socket->setParent(this);

    connect(m_socket, &QLocalSocket::readyRead, this, &ProbeConnection::onReadyRead);
    connect(m_socket, &QLocalSocket::disconnected, this, &ProbeConnection::onDisconnected);
    connect(m_socket,
            &QLocalSocket::errorOccurred,
            this,
            [this](QLocalSocket::LocalSocketError) {
                if (!m_handshakeComplete) {
                    sendError(QStringLiteral("connectionError"), m_socket->errorString());
                }
            });

    m_topLevelPoll.setInterval(1000);
    m_topLevelPoll.setSingleShot(false);
    connect(&m_topLevelPoll, &QTimer::timeout, this, &ProbeConnection::refreshTopLevelObjects);
}

ProbeConnection::~ProbeConnection()
{
    m_topLevelPoll.stop();
    cleanup();
    if (m_socket) {
        disconnect(m_socket, nullptr, this, nullptr);
        if (m_socket->state() == QLocalSocket::ConnectedState) {
            m_socket->disconnectFromServer();
            m_socket->waitForDisconnected(50);
        }
    }
}

void ProbeConnection::close()
{
    if (m_socket) {
        m_socket->disconnectFromServer();
    }
}

bool ProbeConnection::eventFilter(QObject *watched, QEvent *event)
{
    if (!watched || !event) {
        return QObject::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::ChildAdded: {
        auto *childEvent = static_cast<QChildEvent *>(event);
        if (!childEvent->added()) {
            break;
        }
        QObject *child = childEvent->child();
        if (!child || m_tracked.contains(child)) {
            break;
        }
        const QString parentId = ensureIdForObject(watched);
        installRecursive(child, parentId, true);
        break;
    }
    case QEvent::ChildRemoved: {
        auto *childEvent = static_cast<QChildEvent *>(event);
        QObject *child = childEvent->child();
        if (!child) {
            break;
        }
        removeRecursive(child, true);
        break;
    }
    case QEvent::DynamicPropertyChange: {
        auto *dynamicEvent = static_cast<QDynamicPropertyChangeEvent *>(event);
        const QByteArray name = dynamicEvent->propertyName();
        emitPropertiesChanged(watched, {QString::fromUtf8(name)});
        break;
    }
    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}

void ProbeConnection::onReadyRead()
{
    if (!m_socket) {
        return;
    }

    m_readBuffer += m_socket->readAll();
    processBuffer();
}

void ProbeConnection::onDisconnected()
{
    m_topLevelPoll.stop();
    cleanup();
    emit closed(this);
}

void ProbeConnection::handlePropertyNotify()
{
    QObject *object = sender();
    if (!object) {
        return;
    }

    const int signalIndex = senderSignalIndex();
    const auto key = qMakePair(static_cast<const QObject *>(object), signalIndex);
    const QString propertyName = m_propertyBySignalIndex.value(key);
    if (propertyName.isEmpty()) {
        emitPropertiesChanged(object, {});
    } else {
        emitPropertiesChanged(object, {propertyName});
    }
}

void ProbeConnection::onObjectDestroyed(QObject *object)
{
    if (!object) {
        return;
    }

    removeRecursive(object, true);

    const QString id = m_idsByObject.take(object);
    if (!id.isEmpty()) {
        m_objectById.remove(id);
    }
}

void ProbeConnection::processBuffer()
{
    while (m_readBuffer.size() >= 4) {
        const quint32 length = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(m_readBuffer.constData()));
        if (m_readBuffer.size() < static_cast<int>(length) + 4) {
            break;
        }

        QByteArray payload = m_readBuffer.mid(4, static_cast<int>(length));
        m_readBuffer.remove(0, static_cast<int>(length) + 4);

        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            sendError(QStringLiteral("invalidJson"),
                      QStringLiteral("Unable to parse message: %1").arg(parseError.errorString()));
            continue;
        }

        handleMessage(document.object());
    }
}

void ProbeConnection::handleMessage(const QJsonObject &message)
{
    const QString type = message.value(QLatin1String(protocol::keys::kType)).toString();
    if (type.isEmpty()) {
        sendError(QStringLiteral("invalidMessage"), QStringLiteral("Message missing 'type'."));
        return;
    }

    if (!m_handshakeComplete && type != QLatin1String(protocol::types::kAttach)) {
        sendError(QStringLiteral("handshakeRequired"),
                  QStringLiteral("Must attach before sending '%1'.").arg(type));
        return;
    }

    if (type == QLatin1String(protocol::types::kAttach)) {
        handleAttach(message);
    } else if (type == QLatin1String(protocol::types::kSnapshotRequest)) {
        handleSnapshotRequest(message);
    } else if (type == QLatin1String(protocol::types::kPropertiesRequest)) {
        handlePropertiesRequest(message);
    } else if (type == QLatin1String(protocol::types::kSelectNode)) {
        handleSelectNode(message);
    } else if (type == QLatin1String(protocol::types::kDetach)) {
        handleDetach(message);
    } else {
        sendError(QStringLiteral("unknownMessage"),
                  QStringLiteral("Unknown message type '%1'.").arg(type));
    }
}

void ProbeConnection::handleAttach(const QJsonObject &message)
{
    if (m_handshakeComplete) {
        sendError(QStringLiteral("alreadyAttached"),
                  QStringLiteral("Attach has already been completed."));
        return;
    }

    const int clientVersion =
        message.value(QLatin1String(protocol::keys::kProtocolVersion)).toInt(-1);
    if (clientVersion != qt_spy::protocol::kVersion) {
        QJsonObject context;
        context[QStringLiteral("serverVersion")] = qt_spy::protocol::kVersion;
        context[QStringLiteral("clientVersion")] = clientVersion;
        sendError(QStringLiteral("protocolMismatch"),
                  QStringLiteral("Protocol mismatch between client and helper."),
                  context);
        if (m_socket) {
            m_socket->disconnectFromServer();
        }
        return;
    }

    m_handshakeComplete = true;
    if (m_probe) {
        const QString clientName =
            message.value(QLatin1String(protocol::keys::kClientName)).toString();
        qInfo() << "qt-spy probe attached client" << (clientName.isEmpty() ? QStringLiteral("<unknown>") : clientName);
    }
    sendHello();

    ensureRootsTracked(false);
    m_topLevelPoll.start();
}

void ProbeConnection::handleDetach(const QJsonObject &message)
{
    if (!m_handshakeComplete) {
        sendError(QStringLiteral("invalidState"),
                  QStringLiteral("Cannot detach before completing attach."));
        return;
    }

    QJsonObject payload;
    payload[QLatin1String(protocol::keys::kType)] = QLatin1String(protocol::types::kGoodbye);
    payload[QLatin1String(protocol::keys::kTimestampMs)] =
        static_cast<qint64>(QDateTime::currentMSecsSinceEpoch());
    if (message.contains(QLatin1String(protocol::keys::kRequestId))) {
        payload[QLatin1String(protocol::keys::kRequestId)] =
            message.value(QLatin1String(protocol::keys::kRequestId));
    }

    sendMessage(payload);

    m_handshakeComplete = false;
    m_topLevelPoll.stop();
    cleanup();

    if (m_socket) {
        m_socket->flush();
        m_socket->disconnectFromServer();
    }
}

void ProbeConnection::handleSnapshotRequest(const QJsonObject &message)
{
    QJsonObject payload = buildSnapshotPayload();
    if (message.contains(QLatin1String(protocol::keys::kRequestId))) {
        payload[QLatin1String(protocol::keys::kRequestId)] =
            message.value(QLatin1String(protocol::keys::kRequestId));
    }
    sendMessage(payload);
}

void ProbeConnection::handlePropertiesRequest(const QJsonObject &message)
{
    const QString id = message.value(QLatin1String(protocol::keys::kId)).toString();
    if (id.isEmpty()) {
        sendError(QStringLiteral("invalidRequest"),
                  QStringLiteral("propertiesRequest requires an 'id'."));
        return;
    }

    const auto object = m_objectById.value(id);
    if (object.isNull()) {
        QJsonObject context;
        context[QLatin1String(protocol::keys::kId)] = id;
        sendError(QStringLiteral("unknownNode"),
                  QStringLiteral("No QObject is tracked with the requested id."),
                  context);
        return;
    }

    QJsonObject payload;
    payload[QLatin1String(protocol::keys::kType)] = QLatin1String(protocol::types::kProperties);
    payload[QLatin1String(protocol::keys::kTimestampMs)] =
        static_cast<qint64>(QDateTime::currentMSecsSinceEpoch());
    payload[QLatin1String(protocol::keys::kId)] = id;
    payload[QLatin1String(protocol::keys::kProperties)] = serializeProperties(object.data());
    if (message.contains(QLatin1String(protocol::keys::kRequestId))) {
        payload[QLatin1String(protocol::keys::kRequestId)] =
            message.value(QLatin1String(protocol::keys::kRequestId));
    }
    sendMessage(payload);
}

void ProbeConnection::handleSelectNode(const QJsonObject &message)
{
    const QString id = message.value(QLatin1String(protocol::keys::kId)).toString();
    if (id.isEmpty()) {
        sendError(QStringLiteral("invalidRequest"),
                  QStringLiteral("selectNode requires an 'id'."));
        return;
    }

    const auto object = m_objectById.value(id);
    if (object.isNull()) {
        QJsonObject context;
        context[QLatin1String(protocol::keys::kId)] = id;
        sendError(QStringLiteral("unknownNode"),
                  QStringLiteral("Cannot select an unknown node."),
                  context);
        return;
    }

    m_selectedId = id;

    QJsonObject payload;
    payload[QLatin1String(protocol::keys::kType)] = QLatin1String(protocol::types::kSelectionAck);
    payload[QLatin1String(protocol::keys::kTimestampMs)] =
        static_cast<qint64>(QDateTime::currentMSecsSinceEpoch());
    payload[QLatin1String(protocol::keys::kId)] = id;
    if (message.contains(QLatin1String(protocol::keys::kRequestId))) {
        payload[QLatin1String(protocol::keys::kRequestId)] =
            message.value(QLatin1String(protocol::keys::kRequestId));
    }
    sendMessage(payload);
}

void ProbeConnection::sendMessage(const QJsonObject &message)
{
    if (!m_socket) {
        return;
    }

    QJsonDocument document(message);
    const QByteArray payload = document.toJson(QJsonDocument::Compact);

    QByteArray frame;
    frame.resize(4);
    qToBigEndian(static_cast<quint32>(payload.size()), reinterpret_cast<uchar *>(frame.data()));
    frame.append(payload);

    m_socket->write(frame);
    m_socket->flush();
}

void ProbeConnection::sendError(const QString &code, const QString &text, const QJsonObject &context)
{
    QJsonObject payload;
    payload[QLatin1String(protocol::keys::kType)] = QLatin1String(protocol::types::kError);
    payload[QLatin1String(protocol::keys::kTimestampMs)] =
        static_cast<qint64>(QDateTime::currentMSecsSinceEpoch());
    payload[QStringLiteral("code")] = code;
    payload[QStringLiteral("message")] = text;
    if (!context.isEmpty()) {
        payload[QStringLiteral("context")] = context;
    }
    sendMessage(payload);
}

void ProbeConnection::sendHello()
{
    QJsonObject payload;
    payload[QLatin1String(protocol::keys::kType)] = QLatin1String(protocol::types::kHello);
    payload[QLatin1String(protocol::keys::kTimestampMs)] =
        static_cast<qint64>(QDateTime::currentMSecsSinceEpoch());
    payload[QStringLiteral("protocolVersion")] = qt_spy::protocol::kVersion;
    payload[QLatin1String(protocol::keys::kServerName)] =
        m_probe ? m_probe->serverName() : QString();
    payload[QLatin1String(protocol::keys::kApplicationPid)] =
        static_cast<qint64>(QCoreApplication::applicationPid());
    payload[QLatin1String(protocol::keys::kApplicationName)] =
        QCoreApplication::applicationName();
    sendMessage(payload);
}

QJsonObject ProbeConnection::buildSnapshotPayload()
{
    const QVector<QObject *> roots = ensureRootsTracked(false);

    QSet<const QObject *> visited;
    QJsonArray nodes;
    QJsonArray rootIds;

    std::function<void(QObject *, const QString &)> visit = [&](QObject *object, const QString &parentId) {
        if (!object || visited.contains(object)) {
            return;
        }
        visited.insert(object);

        const QString id = ensureIdForObject(object);
        if (parentId.isEmpty()) {
            rootIds.append(id);
        }

        nodes.append(serializeNode(object, parentId));

        const QList<QObject *> children = object->children();
        for (QObject *child : children) {
            visit(child, id);
        }
    };

    for (QObject *root : roots) {
        visit(root, QString());
    }

    QJsonObject payload;
    payload[QLatin1String(protocol::keys::kType)] = QLatin1String(protocol::types::kSnapshot);
    payload[QLatin1String(protocol::keys::kTimestampMs)] =
        static_cast<qint64>(QDateTime::currentMSecsSinceEpoch());
    payload[QStringLiteral("protocolVersion")] = qt_spy::protocol::kVersion;
    payload[QLatin1String(protocol::keys::kServerName)] =
        m_probe ? m_probe->serverName() : QString();
    payload[QLatin1String(protocol::keys::kNodes)] = nodes;
    payload[QLatin1String(protocol::keys::kRootIds)] = rootIds;
    if (!m_selectedId.isEmpty()) {
        payload[QLatin1String(protocol::keys::kSelection)] = m_selectedId;
    }

    return payload;
}

QJsonObject ProbeConnection::serializeNode(QObject *object, const QString &parentId)
{
    QJsonObject node;
    const QString id = ensureIdForObject(object);

    node[QLatin1String(protocol::keys::kId)] = id;
    if (!parentId.isEmpty()) {
        node[QLatin1String(protocol::keys::kParentId)] = parentId;
    }
    node[QStringLiteral("className")] = QString::fromLatin1(object->metaObject()->className());
    node[QStringLiteral("objectName")] = object->objectName();
    node[QStringLiteral("address")] =
        QStringLiteral("0x%1").arg(quintptr(object), 0, 16, QLatin1Char('0'));

    QJsonArray childrenIds;
    const QList<QObject *> children = object->children();
    for (QObject *child : children) {
        childrenIds.append(ensureIdForObject(child));
    }
    if (!childrenIds.isEmpty()) {
        node[QLatin1String(protocol::keys::kChildIds)] = childrenIds;
    }

    if (auto *widget = qobject_cast<QWidget *>(object)) {
        node[QStringLiteral("widget")] = widgetInfo(widget);
    }
    if (auto *window = qobject_cast<QWindow *>(object)) {
        node[QStringLiteral("window")] = windowInfo(window);
    }

    QJsonObject properties = serializeProperties(object);
    if (!properties.isEmpty()) {
        node[QLatin1String(protocol::keys::kProperties)] = properties;
    }

    return node;
}

QJsonObject ProbeConnection::serializeProperties(QObject *object) const
{
    QJsonObject properties;
    const QMetaObject *meta = object->metaObject();
    for (int i = 0; i < meta->propertyCount(); ++i) {
        const QMetaProperty property = meta->property(i);
        if (!property.isReadable()) {
            continue;
        }
        const QVariant value = property.read(object);
        if (!value.isValid()) {
            continue;
        }
        properties[QString::fromLatin1(property.name())] = variantToJson(value);
    }

    const auto dynamicNames = object->dynamicPropertyNames();
    if (!dynamicNames.isEmpty()) {
        QJsonObject dynamicProps;
        for (const QByteArray &name : dynamicNames) {
            const QVariant value = object->property(name.constData());
            dynamicProps[QString::fromUtf8(name)] = variantToJson(value);
        }
        if (!dynamicProps.isEmpty()) {
            properties[QStringLiteral("__dynamic")] = dynamicProps;
        }
    }

    return properties;
}

QString ProbeConnection::ensureIdForObject(const QObject *object)
{
    if (!object) {
        return {};
    }

    const auto existing = m_idsByObject.constFind(object);
    if (existing != m_idsByObject.constEnd()) {
        const QString id = existing.value();
        m_objectById.insert(id, const_cast<QObject *>(object));
        return id;
    }

    const QString id = makeNodeId(object);
    m_idsByObject.insert(object, id);
    m_objectById.insert(id, const_cast<QObject *>(object));
    return id;
}

void ProbeConnection::observeProperties(QObject *object)
{
    const int slotIndex = metaObject()->indexOfSlot("handlePropertyNotify()");
    if (slotIndex < 0) {
        return;
    }

    QVector<QMetaObject::Connection> connections;

    for (int metaIndex = 0; metaIndex < object->metaObject()->propertyCount(); ++metaIndex) {
        const QMetaProperty property = object->metaObject()->property(metaIndex);
        if (!property.hasNotifySignal()) {
            continue;
        }
        const int signalIndex = property.notifySignalIndex();
        if (signalIndex < 0) {
            continue;
        }
        const auto key = qMakePair(static_cast<const QObject *>(object), signalIndex);
        if (m_propertyBySignalIndex.contains(key)) {
            continue;
        }
        QMetaObject::Connection connection =
            QMetaObject::connect(object, signalIndex, this, slotIndex);
        if (!connection) {
            continue;
        }
        connections.append(connection);
        m_propertyBySignalIndex.insert(key, QString::fromLatin1(property.name()));
    }

    if (!connections.isEmpty()) {
        m_propertyConnections.insert(object, connections);
    }
}

void ProbeConnection::unobserveProperties(QObject *object)
{
    auto connectionIt = m_propertyConnections.find(object);
    if (connectionIt != m_propertyConnections.end()) {
        for (const QMetaObject::Connection &connection : connectionIt.value()) {
            QObject::disconnect(connection);
        }
        m_propertyConnections.erase(connectionIt);
    }

    for (auto it = m_propertyBySignalIndex.begin(); it != m_propertyBySignalIndex.end();) {
        if (it.key().first == object) {
            it = m_propertyBySignalIndex.erase(it);
        } else {
            ++it;
        }
    }
}

void ProbeConnection::installRecursive(QObject *object, const QString &parentId, bool announce)
{
    if (!object) {
        return;
    }

    const QString id = ensureIdForObject(object);
    m_parentByObject.insert(object, parentId);

    const bool alreadyTracked = m_tracked.contains(object);
    if (!alreadyTracked) {
        m_tracked.insert(object);
        object->installEventFilter(this);
        QObject::connect(object,
                         &QObject::destroyed,
                         this,
                         &ProbeConnection::onObjectDestroyed,
                         Qt::UniqueConnection);
        observeProperties(object);
    }

    if (announce && !alreadyTracked) {
        emitNodeAdded(object, parentId);
    }

    const QList<QObject *> children = object->children();
    for (QObject *child : children) {
        installRecursive(child, id, announce);
    }
}

void ProbeConnection::removeRecursive(QObject *object, bool emitEvent)
{
    if (!object || !m_tracked.contains(object)) {
        return;
    }

    const QList<QObject *> children = object->children();
    for (QObject *child : children) {
        removeRecursive(child, emitEvent);
    }

    object->removeEventFilter(this);
    QObject::disconnect(object, nullptr, this, nullptr);
    unobserveProperties(object);

    m_tracked.remove(object);
    const QString parentId = m_parentByObject.take(object);
    const QString id = m_idsByObject.value(object);

    if (emitEvent && !id.isEmpty()) {
        emitNodeRemoved(id, parentId);
    }
}

void ProbeConnection::emitNodeAdded(QObject *object, const QString &parentId)
{
    QJsonObject payload;
    payload[QLatin1String(protocol::keys::kType)] = QLatin1String(protocol::types::kNodeAdded);
    payload[QLatin1String(protocol::keys::kTimestampMs)] =
        static_cast<qint64>(QDateTime::currentMSecsSinceEpoch());
    if (!parentId.isEmpty()) {
        payload[QLatin1String(protocol::keys::kParentId)] = parentId;
    }
    payload[QLatin1String(protocol::keys::kNode)] = serializeNode(object, parentId);
    sendMessage(payload);
}

void ProbeConnection::emitNodeRemoved(const QString &id, const QString &parentId)
{
    QJsonObject payload;
    payload[QLatin1String(protocol::keys::kType)] = QLatin1String(protocol::types::kNodeRemoved);
    payload[QLatin1String(protocol::keys::kTimestampMs)] =
        static_cast<qint64>(QDateTime::currentMSecsSinceEpoch());
    payload[QLatin1String(protocol::keys::kId)] = id;
    if (!parentId.isEmpty()) {
        payload[QLatin1String(protocol::keys::kParentId)] = parentId;
    }
    sendMessage(payload);
}

void ProbeConnection::emitPropertiesChanged(QObject *object, const QStringList &names)
{
    if (!object) {
        return;
    }

    const QString id = ensureIdForObject(object);
    if (id.isEmpty()) {
        return;
    }

    QJsonObject payload;
    payload[QLatin1String(protocol::keys::kType)] =
        QLatin1String(protocol::types::kPropertiesChanged);
    payload[QLatin1String(protocol::keys::kTimestampMs)] =
        static_cast<qint64>(QDateTime::currentMSecsSinceEpoch());
    payload[QLatin1String(protocol::keys::kId)] = id;
    if (!names.isEmpty()) {
        QJsonArray changed;
        for (const QString &name : names) {
            changed.append(name);
        }
        payload[QLatin1String(protocol::keys::kChanged)] = changed;
    }
    payload[QLatin1String(protocol::keys::kProperties)] = serializeProperties(object);
    sendMessage(payload);
}

QVector<QObject *> ProbeConnection::ensureRootsTracked(bool announce)
{
    QVector<QObject *> roots;
    QSet<QObject *> candidates;

    if (auto *app = QCoreApplication::instance()) {
        const auto topWidgets = QApplication::topLevelWidgets();
        for (QWidget *widget : topWidgets) {
            candidates.insert(widget);
        }

        const auto topWindows = QGuiApplication::topLevelWindows();
        for (QWindow *window : topWindows) {
            candidates.insert(window);
        }

        const auto appChildren = app->children();
        for (QObject *child : appChildren) {
            candidates.insert(child);
        }
    }

    for (QObject *candidate : std::as_const(candidates)) {
        QObject *parent = candidate->parent();
        bool skip = false;
        while (parent) {
            if (candidates.contains(parent)) {
                skip = true;
                break;
            }
            parent = parent->parent();
        }
        if (!skip) {
            roots.append(candidate);
        }
    }

    for (QObject *root : roots) {
        installRecursive(root, QString(), announce);
    }

    return roots;
}

void ProbeConnection::refreshTopLevelObjects()
{
    ensureRootsTracked(true);
}

void ProbeConnection::cleanup()
{
    const auto trackedSnapshot = m_tracked;
    for (const QObject *object : trackedSnapshot) {
        removeRecursive(const_cast<QObject *>(object), false);
    }

    m_propertyConnections.clear();
    m_propertyBySignalIndex.clear();
    m_parentByObject.clear();
    m_idsByObject.clear();
    m_objectById.clear();
    m_tracked.clear();
    m_selectedId.clear();
}

// Probe implementation

Probe::Probe(const ProbeOptions &options, QObject *parent)
    : QObject(parent)
    , m_serverName(options.serverName.isEmpty() ? defaultServerName() : options.serverName)
    , m_autoStart(options.autoStart)
{
    if (m_autoStart) {
        QMetaObject::invokeMethod(this, &Probe::start, Qt::QueuedConnection);
    }
}

QString Probe::serverName() const
{
    return m_serverName;
}

bool Probe::isListening() const
{
    return m_server && m_server->isListening();
}

void Probe::start()
{
    if (m_server) {
        return;
    }
    setupServer();
}

void Probe::stop()
{
    if (!m_server) {
        return;
    }

    for (ProbeConnection *connection : std::as_const(m_connections)) {
        if (connection) {
            connection->close();
            connection->deleteLater();
        }
    }
    m_connections.clear();

    m_server->close();
    QLocalServer::removeServer(m_serverName);
    m_server.reset();
}

void Probe::setupServer()
{
    auto server = std::make_unique<QLocalServer>(this);
    QLocalServer::removeServer(m_serverName);
    if (!server->listen(m_serverName)) {
        qWarning() << "qt-spy: failed to listen on" << m_serverName << server->errorString();
        return;
    }

    connect(server.get(), &QLocalServer::newConnection, this, &Probe::handleNewConnection);
    m_server = std::move(server);
    qInfo() << "qt-spy probe listening on" << m_serverName;
}

void Probe::handleNewConnection()
{
    if (!m_server) {
        return;
    }

    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        auto *connection = new ProbeConnection(socket, this);
        connect(connection, &ProbeConnection::closed, this, &Probe::removeConnection);
        m_connections.push_back(connection);
    }
}

void Probe::removeConnection(ProbeConnection *connection)
{
    m_connections.removeOne(connection);
    if (connection) {
        connection->deleteLater();
    }
}

QString defaultServerName()
{
    QString appName;
    if (const auto *app = QCoreApplication::instance(); app) {
        appName = app->applicationName();
    }
    return defaultServerName(appName, QCoreApplication::applicationPid());
}

QString defaultServerName(qint64 pid)
{
    return defaultServerName(QString(), pid);
}

QString defaultServerName(const QString &applicationName, qint64 pid)
{
    QString sanitized = sanitizeProcessName(applicationName);
#if defined(Q_OS_UNIX)
    if (sanitized.isEmpty()) {
        QFile commFile(QStringLiteral("/proc/%1/comm").arg(pid));
        if (commFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray line = commFile.readLine();
            const QString processName = QString::fromUtf8(line).trimmed();
            sanitized = sanitizeProcessName(processName);
        }
    }
#endif
    if (!sanitized.isEmpty()) {
        return QStringLiteral("qt_spy_%1_%2").arg(sanitized, QString::number(pid));
    }
    return QStringLiteral("qt_spy_%1").arg(QString::number(pid));
}

} // namespace qt_spy

#include "probe.moc"
