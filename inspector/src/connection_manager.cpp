#include "connection_manager.h"
#include "qt_spy/bridge_client.h"
#include "qt_spy/protocol.h"
#include "qt_spy/probe.h"

#include <QTimer>
#include <QDir>
#include <QDebug>
#include <QThread>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace qt_spy {

ConnectionManager::ConnectionManager(QObject *parent)
    : QObject(parent)
    , m_bridge(new BridgeClient(this))
    , m_state(Disconnected)
    , m_pid(0)
    , m_currentServerIndex(0)
    , m_retryTimer(new QTimer(this))
    , m_retryCount(0)
{
    m_retryTimer->setSingleShot(true);
    
    // Connect bridge client signals
    connect(m_bridge, &BridgeClient::socketConnected, this, &ConnectionManager::onSocketConnected);
    connect(m_bridge, &BridgeClient::socketDisconnected, this, &ConnectionManager::onSocketDisconnected);
    connect(m_bridge, &BridgeClient::socketError, this, &ConnectionManager::onSocketError);
    connect(m_bridge, &BridgeClient::helloReceived, this, &ConnectionManager::onHelloReceived);
    connect(m_bridge, &BridgeClient::goodbyeReceived, this, &ConnectionManager::onGoodbyeReceived);
    
    connect(m_retryTimer, &QTimer::timeout, this, &ConnectionManager::onRetryTimer);
}

QString ConnectionManager::statusText() const {
    switch (m_state) {
    case Disconnected:
        return "Disconnected";
    case Connecting:
        return "Connecting...";
    case Connected:
        return "Connected";
    case Attached:
        if (!m_processName.isEmpty() && m_pid > 0) {
            return QString("Attached to %1 (PID: %2)").arg(m_processName).arg(m_pid);
        }
        return "Attached";
    case Error:
        return "Connection Error";
    }
    return "Unknown";
}

void ConnectionManager::connectToProcess(const QtProcessInfo &processInfo) {
    if (m_state != Disconnected) {
        disconnect();
    }
    
    m_processInfo = processInfo;
    m_pid = processInfo.pid;
    m_processName = processInfo.name;
    
    // Don't try to inject into ourselves (this would cause deadlock)
    if (processInfo.name == "qt_spy_inspector" || processInfo.pid == QCoreApplication::applicationPid()) {
        setState(Error);
        emit connectionError("Cannot inject probe into the inspector itself");
        return;
    }
    
    // If no probe is detected, attempt injection first
    if (!processInfo.hasExistingProbe) {
        qDebug() << "ConnectionManager: Injecting qt-spy probe into" << processInfo.name;
        if (!injectProbe(processInfo)) {
            setState(Error);
            emit connectionError(QString("Failed to inject probe into %1 (PID: %2)").arg(processInfo.name).arg(processInfo.pid));
            return;
        }
        
        // Wait a moment for probe to initialize
        QThread::msleep(1000);
    }
    
    m_serverNames = generateServerNames(processInfo);
    m_currentServerIndex = 0;
    m_retryCount = 0;
    
    if (m_serverNames.isEmpty()) {
        setState(Error);
        emit connectionError("Unable to generate server names for process");
        return;
    }
    
    setState(Connecting);
    m_bridge->connectToServer(m_serverNames.first());
}

void ConnectionManager::disconnect() {
    stopRetryTimer();
    resetConnectionState();
    
    if (m_bridge->state() != QLocalSocket::UnconnectedState) {
        m_bridge->disconnectFromServer();
    }
    
    setState(Disconnected);
}

void ConnectionManager::reconnect() {
    if (m_processInfo.pid <= 0) {
        return;
    }
    
    disconnect();
    connectToProcess(m_processInfo);
}

void ConnectionManager::onSocketConnected() {

    setState(Connected);
    m_retryCount = 0;
    
    // Send attach request (use application name like CLI does)
    const QString clientName = QCoreApplication::applicationName();

    m_bridge->sendAttach(clientName, protocol::kVersion);
    
    // Set up a timeout for hello response
    QTimer::singleShot(5000, [this]() {
        if (m_state == Connected) {
            setState(Error);
            emit connectionError("No response from probe after attach request");
        }
    });
}

void ConnectionManager::onSocketDisconnected() {
    if (m_state == Disconnected) {
        return; // Already handled
    }
    
    setState(Disconnected);
    emit detached();
    
    // Try to reconnect if not intentionally disconnected
    if (m_retryCount < MaxRetries && !m_serverNames.isEmpty()) {
        startRetryTimer();
    }
}

void ConnectionManager::onSocketError(QLocalSocket::LocalSocketError error, const QString &message) {

    
    // Try next server name if available
    if (tryNextServerName()) {
        return;
    }
    
    setState(Error);
    emit connectionError(QString("Socket error: %1 (tried %2 server names)").arg(message).arg(m_serverNames.size()));
    
    // Schedule retry
    if (m_retryCount < MaxRetries) {
        startRetryTimer();
    }
}

void ConnectionManager::onHelloReceived(const QJsonObject &message) {

    
    const QString appName = message.value(QLatin1String(protocol::keys::kApplicationName)).toString();
    const qint64 appPid = message.value(QLatin1String(protocol::keys::kApplicationPid)).toInt();
    

    
    m_processName = appName.isEmpty() ? m_processName : appName;
    m_pid = appPid > 0 ? appPid : m_pid;
    
    setState(Attached);
    emit attached(m_processName, m_pid);
}

void ConnectionManager::onGoodbyeReceived(const QJsonObject &message) {
    Q_UNUSED(message)
    setState(Disconnected);
    emit detached();
}

void ConnectionManager::onRetryTimer() {
    m_retryCount++;
    
    if (m_retryCount >= MaxRetries) {
        setState(Error);
        emit connectionError("Maximum retry attempts exceeded");
        return;
    }
    
    // Reset to first server name and try again
    m_currentServerIndex = 0;
    if (!m_serverNames.isEmpty()) {
        setState(Connecting);
        m_bridge->connectToServer(m_serverNames.first());
    }
}

void ConnectionManager::setState(ConnectionState state) {
    if (m_state != state) {
        m_state = state;
        emit stateChanged(state);
        emit statusChanged(statusText());
    }
}

void ConnectionManager::startRetryTimer() {
    const int delay = 1000 * (m_retryCount + 1); // Increasing delay
    m_retryTimer->start(delay);
}

void ConnectionManager::stopRetryTimer() {
    m_retryTimer->stop();
}

QStringList ConnectionManager::generateServerNames(const QtProcessInfo &processInfo) {
    QStringList names;
    
    // First, check for existing sockets in /tmp for this PID
    QDir tmpDir("/tmp");
    QStringList socketFilters;
    socketFilters << QString("qt_spy_*_%1").arg(processInfo.pid);
    QStringList existingSockets = tmpDir.entryList(socketFilters, QDir::AllEntries | QDir::System);
    
    // Add existing sockets as primary candidates
    for (const QString &socketFile : existingSockets) {
        names << socketFile;
    }
    
    // Add generated candidates as fallbacks
    const QString primary = qt_spy::defaultServerName(processInfo.name, processInfo.pid);
    if (!primary.isEmpty() && !names.contains(primary)) {
        names << primary;
    }
    
    const QString fallback = qt_spy::defaultServerName(QString(), processInfo.pid);
    if (!fallback.isEmpty() && !names.contains(fallback)) {
        names << fallback;
    }
    
    const QString numeric = QString("qt_spy_%1").arg(processInfo.pid);
    if (!names.contains(numeric)) {
        names << numeric;
    }
    
    return names;
}

bool ConnectionManager::tryNextServerName() {
    m_currentServerIndex++;
    
    if (m_currentServerIndex >= m_serverNames.size()) {
        return false;
    }
    
    // Try next server name
    setState(Connecting);
    m_bridge->connectToServer(m_serverNames.at(m_currentServerIndex));
    return true;
}

void ConnectionManager::resetConnectionState() {
    m_currentServerIndex = 0;
    m_serverNames.clear();
}

bool ConnectionManager::injectProbe(const QtProcessInfo &processInfo) {
#if defined(Q_OS_UNIX)
    // Use the exact same injection script that works with CLI
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString projectRoot = QDir(appDir).absoluteFilePath("../..");
    const QString injectionScript = projectRoot + "/scripts/inject_qt_spy.sh";
    
    if (!QFile::exists(injectionScript)) {
        qDebug() << "ConnectionManager: Injection script not found at" << injectionScript;
        return false;
    }

    qDebug() << "ConnectionManager: Injecting probe into" << processInfo.name << "PID:" << processInfo.pid;

    QProcess injectionProcess;
    injectionProcess.setProgram(injectionScript);
    injectionProcess.setArguments({QString::number(processInfo.pid)});
    injectionProcess.setWorkingDirectory(projectRoot);
    
    injectionProcess.start();
    
    if (!injectionProcess.waitForFinished(30000)) { // 30 second timeout
        qDebug() << "ConnectionManager: Probe injection timed out for" << processInfo.name;
        injectionProcess.kill();
        injectionProcess.waitForFinished(2000);
        return false;
    }
    
    const int exitCode = injectionProcess.exitCode();
    
    if (exitCode == 0) {
        qDebug() << "ConnectionManager: Probe injection succeeded for" << processInfo.name;
        return true;
    } else {
        // Only show error output on failure for troubleshooting
        const QByteArray stderrOutput = injectionProcess.readAllStandardError();
        if (!stderrOutput.isEmpty()) {
            qDebug() << "ConnectionManager: Injection error:" << QString::fromLocal8Bit(stderrOutput).trimmed();
        }
        qDebug() << "ConnectionManager: Probe injection failed for" << processInfo.name 
                 << "(exit code:" << exitCode << ")";
        return false;
    }
#else
    Q_UNUSED(processInfo)
    qDebug() << "ConnectionManager: Probe injection not supported on this platform";
    return false;
#endif
}

} // namespace qt_spy
