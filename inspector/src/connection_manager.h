#pragma once

#include "node_data.h"

#include <QObject>
#include <QLocalSocket>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace qt_spy {

class BridgeClient;

class ConnectionManager : public QObject {
    Q_OBJECT
    
public:
    enum ConnectionState {
        Disconnected,
        Connecting,
        Connected,
        Attached,
        Error
    };
    
    explicit ConnectionManager(QObject *parent = nullptr);
    
    ConnectionState state() const { return m_state; }
    QString statusText() const;
    QString connectedProcessName() const { return m_processName; }
    qint64 connectedPid() const { return m_pid; }
    
    BridgeClient *bridgeClient() const { return m_bridge; }
    
public slots:
    void connectToProcess(const QtProcessInfo &processInfo);
    void disconnect();
    void reconnect();
    bool injectProbe(const QtProcessInfo &processInfo);
    
signals:
    void stateChanged(ConnectionState state);
    void statusChanged(const QString &status);
    void attached(const QString &applicationName, qint64 pid);
    void detached();
    void connectionError(const QString &error);
    
private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QLocalSocket::LocalSocketError error, const QString &message);
    void onHelloReceived(const QJsonObject &message);
    void onGoodbyeReceived(const QJsonObject &message);
    void onRetryTimer();
    
private:
    void setState(ConnectionState state);
    void startRetryTimer();
    void stopRetryTimer();
    QStringList generateServerNames(const QtProcessInfo &processInfo);
    bool tryNextServerName();
    void resetConnectionState();

    BridgeClient *m_bridge;
    ConnectionState m_state;
    QString m_processName;
    qint64 m_pid;
    QtProcessInfo m_processInfo;
    QStringList m_serverNames;
    int m_currentServerIndex;
    QTimer *m_retryTimer;
    int m_retryCount;
    static constexpr int MaxRetries = 3;
};

} // namespace qt_spy
