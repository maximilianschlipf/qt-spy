#include "qt_spy/bridge_client.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QtEndian>

namespace qt_spy {

BridgeClient::BridgeClient(QObject *parent)
    : QObject(parent)
{
    connect(&m_socket, &QLocalSocket::connected, this, &BridgeClient::handleConnected);
    connect(&m_socket, &QLocalSocket::disconnected, this, &BridgeClient::handleDisconnected);
    connect(&m_socket, &QLocalSocket::readyRead, this, &BridgeClient::handleReadyRead);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, &BridgeClient::handleError);
}

void BridgeClient::connectToServer(const QString &serverName)
{
    if (m_socket.state() != QLocalSocket::UnconnectedState) {
        return;
    }

    m_serverName = serverName;
    m_socket.connectToServer(serverName);
}

void BridgeClient::disconnectFromServer()
{
    m_serverName.clear();
    if (m_socket.state() == QLocalSocket::UnconnectedState) {
        return;
    }
    m_socket.disconnectFromServer();
}

QLocalSocket::LocalSocketState BridgeClient::state() const
{
    return m_socket.state();
}

QString BridgeClient::serverName() const
{
    return m_serverName;
}

void BridgeClient::sendAttach(const QString &clientName, int protocolVersion)
{
    QJsonObject message;
    message[QLatin1String(protocol::keys::kType)] = QLatin1String(protocol::types::kAttach);
    message[QLatin1String(protocol::keys::kProtocolVersion)] = protocolVersion;
    if (!clientName.isEmpty()) {
        message[QLatin1String(protocol::keys::kClientName)] = clientName;
    }
    sendRaw(message);
}

void BridgeClient::sendDetach(const QString &requestId)
{
    QJsonObject message;
    message[QLatin1String(protocol::keys::kType)] = QLatin1String(protocol::types::kDetach);
    if (!requestId.isEmpty()) {
        message[QLatin1String(protocol::keys::kRequestId)] = requestId;
    }
    sendRaw(message);
}

void BridgeClient::requestSnapshot(const QString &requestId)
{
    QJsonObject message;
    message[QLatin1String(protocol::keys::kType)] =
        QLatin1String(protocol::types::kSnapshotRequest);
    if (!requestId.isEmpty()) {
        message[QLatin1String(protocol::keys::kRequestId)] = requestId;
    }
    sendRaw(message);
}

void BridgeClient::requestProperties(const QString &id, const QString &requestId)
{
    if (id.isEmpty()) {
        return;
    }

    QJsonObject message;
    message[QLatin1String(protocol::keys::kType)] =
        QLatin1String(protocol::types::kPropertiesRequest);
    message[QLatin1String(protocol::keys::kId)] = id;
    if (!requestId.isEmpty()) {
        message[QLatin1String(protocol::keys::kRequestId)] = requestId;
    }
    sendRaw(message);
}

void BridgeClient::selectNode(const QString &id, const QString &requestId)
{
    if (id.isEmpty()) {
        return;
    }

    QJsonObject message;
    message[QLatin1String(protocol::keys::kType)] = QLatin1String(protocol::types::kSelectNode);
    message[QLatin1String(protocol::keys::kId)] = id;
    if (!requestId.isEmpty()) {
        message[QLatin1String(protocol::keys::kRequestId)] = requestId;
    }
    sendRaw(message);
}

void BridgeClient::sendRaw(const QJsonObject &message)
{
    writeMessage(message);
}

void BridgeClient::handleConnected()
{
    emit socketConnected();
}

void BridgeClient::handleDisconnected()
{
    m_buffer.clear();
    emit socketDisconnected();
}

void BridgeClient::handleError(QLocalSocket::LocalSocketError error)
{
    emit socketError(error, m_socket.errorString());
}

void BridgeClient::handleReadyRead()
{
    m_buffer += m_socket.readAll();
    processIncomingBuffer();
}

void BridgeClient::writeMessage(const QJsonObject &message)
{
    if (m_socket.state() != QLocalSocket::ConnectedState) {
        return;
    }

    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);

    QByteArray frame;
    frame.resize(4);
    qToBigEndian(static_cast<quint32>(payload.size()), reinterpret_cast<uchar *>(frame.data()));
    frame.append(payload);

    m_socket.write(frame);
    m_socket.flush();
}

void BridgeClient::processIncomingBuffer()
{
    while (m_buffer.size() >= 4) {
        const quint32 length = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(m_buffer.constData()));
        if (m_buffer.size() < static_cast<int>(length) + 4) {
            return;
        }

        const QByteArray payload = m_buffer.mid(4, static_cast<int>(length));
        m_buffer.remove(0, static_cast<int>(length) + 4);

        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            QJsonObject errorPayload;
            errorPayload[QLatin1String(protocol::keys::kType)] =
                QLatin1String(protocol::types::kError);
            errorPayload[QStringLiteral("code")] = QStringLiteral("invalidJson");
            errorPayload[QStringLiteral("message")] =
                QStringLiteral("Bridge client failed to parse helper message: %1")
                    .arg(parseError.errorString());
            emit errorReceived(errorPayload);
            continue;
        }

        dispatchMessage(document.object());
    }
}

void BridgeClient::dispatchMessage(const QJsonObject &message)
{
    const QString type = message.value(QLatin1String(protocol::keys::kType)).toString();
    if (type == QLatin1String(protocol::types::kHello)) {
        emit helloReceived(message);
        return;
    }
    if (type == QLatin1String(protocol::types::kSnapshot)) {
        emit snapshotReceived(message);
        return;
    }
    if (type == QLatin1String(protocol::types::kProperties)) {
        emit propertiesReceived(message);
        return;
    }
    if (type == QLatin1String(protocol::types::kSelectionAck)) {
        emit selectionAckReceived(message);
        return;
    }
    if (type == QLatin1String(protocol::types::kNodeAdded)) {
        emit nodeAdded(message);
        return;
    }
    if (type == QLatin1String(protocol::types::kNodeRemoved)) {
        emit nodeRemoved(message);
        return;
    }
    if (type == QLatin1String(protocol::types::kPropertiesChanged)) {
        emit propertiesChanged(message);
        return;
    }
    if (type == QLatin1String(protocol::types::kError)) {
        emit errorReceived(message);
        return;
    }
    if (type == QLatin1String(protocol::types::kGoodbye)) {
        emit goodbyeReceived(message);
        return;
    }

    emit genericMessageReceived(message);
}

} // namespace qt_spy
