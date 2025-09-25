#pragma once

#include "qt_spy/protocol.h"

#include <QObject>
#include <QLocalSocket>
#include <QJsonObject>

namespace qt_spy {

class BridgeClient : public QObject {
    Q_OBJECT
public:
    explicit BridgeClient(QObject *parent = nullptr);

    void connectToServer(const QString &serverName);
    void disconnectFromServer();

    QLocalSocket::LocalSocketState state() const;
    QString serverName() const;

    void sendAttach(const QString &clientName = QString(),
                    int protocolVersion = qt_spy::protocol::kVersion);
    void sendDetach(const QString &requestId = QString());
    void requestSnapshot(const QString &requestId = QString());
    void requestProperties(const QString &id, const QString &requestId = QString());
    void selectNode(const QString &id, const QString &requestId = QString());
    void sendRaw(const QJsonObject &message);

signals:
    void socketConnected();
    void socketDisconnected();
    void socketError(QLocalSocket::LocalSocketError error, const QString &message);

    void helloReceived(const QJsonObject &message);
    void snapshotReceived(const QJsonObject &message);
    void propertiesReceived(const QJsonObject &message);
    void selectionAckReceived(const QJsonObject &message);
    void nodeAdded(const QJsonObject &message);
    void nodeRemoved(const QJsonObject &message);
    void propertiesChanged(const QJsonObject &message);
    void errorReceived(const QJsonObject &message);
    void goodbyeReceived(const QJsonObject &message);
    void genericMessageReceived(const QJsonObject &message);

private slots:
    void handleConnected();
    void handleDisconnected();
    void handleError(QLocalSocket::LocalSocketError error);
    void handleReadyRead();

private:
    void writeMessage(const QJsonObject &message);
    void processIncomingBuffer();
    void dispatchMessage(const QJsonObject &message);

    QString m_serverName;
    QLocalSocket m_socket;
    QByteArray m_buffer;
};

} // namespace qt_spy
