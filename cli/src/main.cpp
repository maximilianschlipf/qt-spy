#include "qt_spy/bridge_client.h"
#include "qt_spy/probe.h"
#include "qt_spy/protocol.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QFileDevice>
#include <QFileInfo>
#include <QProcess>
#include <QRegExp>
#include <QStringList>
#include <QSet>
#include <QTextStream>
#include <QTimer>
#include <QVector>

#include <signal.h>

#include <cstring>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <utility>

namespace {

namespace protocol = qt_spy::protocol;

struct QtProcessInfo {
    qint64 pid = 0;
    QString name;
    QString commandLine;
    QString windowTitle;
    bool hasQtLibraries = false;
    bool hasExistingProbe = false;
    
    QString displayName() const {
        if (!windowTitle.isEmpty()) {
            return QStringLiteral("%1 - \"%2\"").arg(name, windowTitle);
        }
        return name;
    }
};

// Forward declarations
bool checkForQtLibraries(qint64 pid);
bool checkForExistingProbe(qint64 pid);

QVector<QtProcessInfo> discoverQtProcesses() {
    QVector<QtProcessInfo> qtProcesses;
    
#if defined(Q_OS_UNIX)
    QProcess ps;
    ps.start(QStringLiteral("ps"), {QStringLiteral("aux")});
    if (!ps.waitForFinished(5000)) {
        qWarning() << "Failed to execute ps command for process discovery";
        return qtProcesses;
    }
    
    const QByteArray output = ps.readAllStandardOutput();
    const QStringList lines = QString::fromLocal8Bit(output).split('\n', Qt::SkipEmptyParts);
    
    for (int i = 1; i < lines.size(); ++i) { // Skip header line
        const QString line = lines.at(i);
        const QStringList fields = line.split(QRegExp("\\s+"), Qt::SkipEmptyParts);
        
        if (fields.size() < 11) continue;
        
        bool ok = false;
        const qint64 pid = fields.at(1).toLongLong(&ok);
        if (!ok || pid <= 0) continue;
        
        // Reconstruct command line from remaining fields
        QStringList cmdParts = fields.mid(10);
        const QString commandLine = cmdParts.join(' ');
        
        // Extract process info first
        QtProcessInfo info;
        info.pid = pid;
        info.commandLine = commandLine;
        
        // Extract process name
        const int spaceIndex = commandLine.indexOf(' ');
        const QString fullPath = spaceIndex > 0 ? commandLine.left(spaceIndex) : commandLine;
        info.name = QFileInfo(fullPath).baseName();
        
        // Check for Qt libraries by examining memory maps
        info.hasQtLibraries = checkForQtLibraries(pid);
        
        // Only include processes that actually have Qt libraries
        if (info.hasQtLibraries) {
            // Check for existing qt-spy probe
            info.hasExistingProbe = checkForExistingProbe(pid);
            qtProcesses.append(info);
        }
    }
#endif
    
    // Sort by most recent (highest PID typically means more recent)
    std::sort(qtProcesses.begin(), qtProcesses.end(),
              [](const QtProcessInfo &a, const QtProcessInfo &b) {
                  return a.pid > b.pid;
              });
    
    return qtProcesses;
}

bool checkForQtLibraries(qint64 pid) {
#if defined(Q_OS_UNIX)
    const QString mapsPath = QStringLiteral("/proc/%1/maps").arg(pid);
    QFile mapsFile(mapsPath);
    if (!mapsFile.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    const QByteArray content = mapsFile.readAll();
    const QString mapsContent = QString::fromLocal8Bit(content);
    
    // Look for Qt library signatures (more comprehensive patterns)
    return mapsContent.contains("libQt5", Qt::CaseInsensitive) ||
           mapsContent.contains("libQt6", Qt::CaseInsensitive) ||
           mapsContent.contains("Qt5Core", Qt::CaseInsensitive) ||
           mapsContent.contains("Qt6Core", Qt::CaseInsensitive) ||
           mapsContent.contains("Qt5Gui", Qt::CaseInsensitive) ||
           mapsContent.contains("Qt6Gui", Qt::CaseInsensitive) ||
           mapsContent.contains("Qt5Widgets", Qt::CaseInsensitive) ||
           mapsContent.contains("Qt6Widgets", Qt::CaseInsensitive) ||
           mapsContent.contains("Qt5Quick", Qt::CaseInsensitive) ||
           mapsContent.contains("Qt6Quick", Qt::CaseInsensitive) ||
           mapsContent.contains("Qt5Qml", Qt::CaseInsensitive) ||
           mapsContent.contains("Qt6Qml", Qt::CaseInsensitive) ||
           mapsContent.contains("Qt5Pdf", Qt::CaseInsensitive) ||
           mapsContent.contains("Qt6Pdf", Qt::CaseInsensitive);
#else
    Q_UNUSED(pid);
    return false;
#endif
}

// Forward declarations
QString detectProcessName(qint64 pid);

bool checkForExistingProbe(qint64 pid) {
    // Try to connect to potential qt-spy server names for this process
    const QString processName = detectProcessName(pid);
    const QStringList serverNames = {
        qt_spy::defaultServerName(processName, pid),
        qt_spy::defaultServerName(QString(), pid)
    };
    
    for (const QString &serverName : serverNames) {
        if (serverName.isEmpty()) continue;
        
        QLocalSocket testSocket;
        testSocket.connectToServer(serverName);
        if (testSocket.waitForConnected(100)) {
            testSocket.disconnectFromServer();
            return true;
        }
    }
    return false;
}

QtProcessInfo findProcessByName(const QString &name) {
    const QVector<QtProcessInfo> processes = discoverQtProcesses();
    
    for (const QtProcessInfo &process : processes) {
        if (process.name.compare(name, Qt::CaseInsensitive) == 0) {
            return process;
        }
    }
    
    // Try partial matches
    for (const QtProcessInfo &process : processes) {
        if (process.name.contains(name, Qt::CaseInsensitive)) {
            return process;
        }
    }
    
    return QtProcessInfo{};
}

QtProcessInfo findProcessByTitle(const QString &title) {
    const QVector<QtProcessInfo> processes = discoverQtProcesses();
    
    for (const QtProcessInfo &process : processes) {
        if (process.windowTitle.contains(title, Qt::CaseInsensitive)) {
            return process;
        }
    }
    
    return QtProcessInfo{};
}

void printQtProcessList(const QVector<QtProcessInfo> &processes, QTextStream &out) {
    if (processes.isEmpty()) {
        out << "No Qt processes found." << Qt::endl;
        return;
    }
    
    out << "Available Qt processes:" << Qt::endl;
    for (int i = 0; i < processes.size(); ++i) {
        const QtProcessInfo &process = processes.at(i);
        out << QStringLiteral("  [%1] %2 (PID: %3)")
               .arg(i + 1)
               .arg(process.displayName())
               .arg(process.pid);
        
        if (process.hasExistingProbe) {
            out << " [probe active]";
        }
        out << Qt::endl;
    }
}

int selectProcessInteractively(const QVector<QtProcessInfo> &processes, QTextStream &out, QTextStream &in) {
    if (processes.isEmpty()) {
        return -1;
    }
    
    printQtProcessList(processes, out);
    out << Qt::endl;
    out << "Select process [1-" << processes.size() << "] (or 0 to exit): ";
    out.flush();
    
    QString input;
    input = in.readLine().trimmed();
    
    bool ok = false;
    const int choice = input.toInt(&ok);
    
    if (!ok || choice < 0 || choice > processes.size()) {
        return -1;
    }
    
    if (choice == 0) {
        return -1; // User chose to exit
    }
    
    return choice - 1; // Convert to 0-based index
}

QString detectProcessName(qint64 pid)
{
#if defined(Q_OS_UNIX)
    QFile commFile(QStringLiteral("/proc/%1/comm").arg(pid));
    if (commFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QByteArray line = commFile.readLine();
        return QString::fromUtf8(line).trimmed();
    }
#else
    Q_UNUSED(pid);
#endif
    return {};
}

struct ResolvedServerName {
    QStringList names;
    qint64 pid = -1;
    QString error;
};

ResolvedServerName resolveServerName(const QCommandLineParser &parser,
                                     const QCommandLineOption &serverOption,
                                     const QCommandLineOption &pidOption)
{
    if (parser.isSet(serverOption)) {
        ResolvedServerName resolved;
        resolved.names << parser.value(serverOption);
        return resolved;
    }

    if (parser.isSet(pidOption)) {
        bool ok = false;
        const qint64 pid = parser.value(pidOption).toLongLong(&ok);
        if (!ok || pid <= 0) {
            return ResolvedServerName{QStringList(), -1,
                                      QStringLiteral("Invalid PID supplied: %1")
                                          .arg(parser.value(pidOption))};
        }
        const QString processName = detectProcessName(pid);
        QStringList candidates;
        
        // First, check for existing sockets in /tmp for this PID
        QDir tmpDir("/tmp");
        QStringList socketFilters;
        socketFilters << QStringLiteral("qt_spy_*_%1").arg(QString::number(pid));
        QStringList existingSockets = tmpDir.entryList(socketFilters, QDir::AllEntries | QDir::System);
        
        // Add existing sockets as primary candidates
        for (const QString &socketFile : existingSockets) {
            if (!candidates.contains(socketFile)) {
                candidates << socketFile;
            }
        }
        
        // Add generated candidates as fallbacks
        const QString primary = qt_spy::defaultServerName(processName, pid);
        if (!primary.isEmpty() && !candidates.contains(primary)) {
            candidates << primary;
        }
        const QString sanitizedFallback = qt_spy::defaultServerName(QString(), pid);
        if (!sanitizedFallback.isEmpty() && !candidates.contains(sanitizedFallback)) {
            candidates << sanitizedFallback;
        }
        const QString numericFallback = QStringLiteral("qt_spy_%1").arg(QString::number(pid));
        if (!candidates.contains(numericFallback)) {
            candidates << numericFallback;
        }
        if (candidates.isEmpty()) {
            return ResolvedServerName{QStringList(), -1,
                                      QStringLiteral("Unable to derive server name for PID %1.")
                                          .arg(pid)};
        }
        ResolvedServerName resolved;
        resolved.names = candidates;
        resolved.pid = pid;
        return resolved;
    }

    return ResolvedServerName{QStringList(), -1,
                              QStringLiteral("Please provide either --pid or --server.")};
}

ResolvedServerName resolveServerNameEnhanced(const QCommandLineParser &parser,
                                           const QCommandLineOption &serverOption,
                                           const QCommandLineOption &pidOption,
                                           const QCommandLineOption &listOption,
                                           const QCommandLineOption &autoOption,
                                           const QCommandLineOption &nameOption,
                                           const QCommandLineOption &titleOption,
                                           const QCommandLineOption &interactiveOption,
                                           QTextStream &out,
                                           QTextStream &err)
{
    // Handle --list option first
    if (parser.isSet(listOption)) {
        const QVector<QtProcessInfo> processes = discoverQtProcesses();
        printQtProcessList(processes, out);
        exit(EXIT_SUCCESS);
    }
    
    // Use original logic for explicit --server or --pid
    if (parser.isSet(serverOption) || parser.isSet(pidOption)) {
        return resolveServerName(parser, serverOption, pidOption);
    }
    
    // Discover Qt processes for user-friendly options
    const QVector<QtProcessInfo> processes = discoverQtProcesses();
    
    if (processes.isEmpty()) {
        return ResolvedServerName{QStringList(), -1,
                                  QStringLiteral("No Qt processes found. Try running a Qt application first.")};
    }
    
    QtProcessInfo selectedProcess;
    
    // Handle --interactive option
    if (parser.isSet(interactiveOption)) {
        QTextStream in(stdin);
        const int index = selectProcessInteractively(processes, out, in);
        if (index < 0 || index >= processes.size()) {
            return ResolvedServerName{QStringList(), -1,
                                      QStringLiteral("No process selected or invalid selection.")};
        }
        selectedProcess = processes.at(index);
    }
    // Handle --auto option
    else if (parser.isSet(autoOption)) {
        selectedProcess = processes.first(); // Most recent process (sorted by PID descending)
        out << "Auto-attaching to: " << selectedProcess.displayName() 
            << " (PID: " << selectedProcess.pid << ")" << Qt::endl;
    }
    // Handle --name option
    else if (parser.isSet(nameOption)) {
        const QString name = parser.value(nameOption);
        selectedProcess = findProcessByName(name);
        if (selectedProcess.pid == 0) {
            return ResolvedServerName{QStringList(), -1,
                                      QStringLiteral("No Qt process found with name: %1").arg(name)};
        }
        out << "Found process by name: " << selectedProcess.displayName() 
            << " (PID: " << selectedProcess.pid << ")" << Qt::endl;
    }
    // Handle --title option
    else if (parser.isSet(titleOption)) {
        const QString title = parser.value(titleOption);
        selectedProcess = findProcessByTitle(title);
        if (selectedProcess.pid == 0) {
            return ResolvedServerName{QStringList(), -1,
                                      QStringLiteral("No Qt process found with window title containing: %1").arg(title)};
        }
        out << "Found process by title: " << selectedProcess.displayName() 
            << " (PID: " << selectedProcess.pid << ")" << Qt::endl;
    }
    // No specific option - show helpful message
    else {
        err << "Multiple Qt processes available. Use one of these options:" << Qt::endl;
        err << "  --interactive  : Show selection menu" << Qt::endl;
        err << "  --auto         : Auto-attach to most recent process" << Qt::endl;
        err << "  --name <name>  : Attach by process name" << Qt::endl;
        err << "  --list         : Show all available processes" << Qt::endl;
        err << Qt::endl;
        printQtProcessList(processes, err);
        return ResolvedServerName{QStringList(), -1,
                                  QStringLiteral("Please specify which process to attach to.")};
    }
    
    // Convert selected process to ResolvedServerName
    if (selectedProcess.pid > 0) {
        const QString processName = detectProcessName(selectedProcess.pid);
        QStringList candidates;
        
        // First, check for existing sockets in /tmp for this PID
        QDir tmpDir("/tmp");
        QStringList socketFilters;
        socketFilters << QStringLiteral("qt_spy_*_%1").arg(QString::number(selectedProcess.pid));
        QStringList existingSockets = tmpDir.entryList(socketFilters, QDir::AllEntries | QDir::System);
        
        // Add existing sockets as primary candidates
        for (const QString &socketFile : existingSockets) {
            if (!candidates.contains(socketFile)) {
                candidates << socketFile;
            }
        }
        
        // Add generated candidates as fallbacks
        const QString primary = qt_spy::defaultServerName(processName, selectedProcess.pid);
        if (!primary.isEmpty() && !candidates.contains(primary)) {
            candidates << primary;
        }
        const QString sanitizedFallback = qt_spy::defaultServerName(QString(), selectedProcess.pid);
        if (!sanitizedFallback.isEmpty() && !candidates.contains(sanitizedFallback)) {
            candidates << sanitizedFallback;
        }
        const QString numericFallback = QStringLiteral("qt_spy_%1").arg(QString::number(selectedProcess.pid));
        if (!candidates.contains(numericFallback)) {
            candidates << numericFallback;
        }
        
        ResolvedServerName resolved;
        resolved.names = candidates;
        resolved.pid = selectedProcess.pid;
        return resolved;
    }
    
    return ResolvedServerName{QStringList(), -1,
                              QStringLiteral("Failed to resolve process information.")};
}

struct ActionTarget {
    enum class Kind { None, Id, FirstRoot };

    Kind kind = Kind::None;
    QString value;   // used when kind == Id
    bool sticky = false;
    bool completed = false;

    bool pending() const { return kind != Kind::None && !completed; }

    void markCompleted()
    {
        if (sticky) {
            completed = true;
            return;
        }
        kind = Kind::None;
        value.clear();
        completed = false;
    }

    void resetForReconnect()
    {
        if (sticky) {
            completed = false;
        }
    }

    void clear()
    {
        kind = Kind::None;
        value.clear();
        sticky = false;
        completed = false;
    }
};

struct ClientOptions {
    QStringList serverNames;
    int maxRetries = -1; // -1 == infinite
    ActionTarget selectTarget;
    ActionTarget propertiesTarget;
    bool snapshotOnce = false;
    qint64 targetPid = -1;
    bool enableInjection = true;
};

class Client : public QObject {
    Q_OBJECT
public:
    explicit Client(ClientOptions options, QObject *parent = nullptr);

    void start();

public slots:
    void requestGracefulShutdown() { exitWithCode(EXIT_SUCCESS); }

signals:
    void finished(int exitCode);

private slots:
    void connectToServer();
    void onConnected();
    void onDisconnected();
    void onSocketError(QLocalSocket::LocalSocketError error, const QString &message);
    void retryTimeout();

private:
    void sendAttach();
    void sendSnapshotRequest();
    void requestProperties(const QString &id);
    void sendSelect(const QString &id);
    void handleHello(const QJsonObject &message);
    void handleSnapshot(const QJsonObject &message);
    void handlePropertiesMessage(const QJsonObject &message);
    void handleSelectionAck(const QJsonObject &message);
    void handleGenericMessage(const QJsonObject &message);
    void handleErrorMessage(const QJsonObject &message);
    void handleGoodbye(const QJsonObject &message);
    void scheduleReconnect();
    void resetConnectionState();
    QString currentServerName() const;
    bool advanceServerNameForRetry(bool dropCurrent = false);
    bool attemptInjection();
    QString nextRequestId();
    void resolveDeferredTargets(const QJsonArray &rootIds);
    void completeTarget(ActionTarget &target);
    void exitWithCode(int code);
    void beginDetachHandshake();
    void handleDetachTimeout();
    void finalizeExit();

    ClientOptions m_options;
    qt_spy::BridgeClient m_bridge;
    QTextStream m_stdout;
    QTextStream m_stderr;
    QTimer m_retryTimer;
    QTimer m_detachTimer;
    bool m_attachSent = false;
    bool m_attached = false;
    int m_retryAttempt = 0;
    quint64 m_requestCounter = 0;
    bool m_exiting = false;
    bool m_injectionAttempted = false;
    bool m_injectionSucceeded = false;
    bool m_detachRequested = false;
    bool m_exitSignalled = false;
    int m_exitCode = EXIT_SUCCESS;
    QString m_pendingDetachRequestId;
    bool m_serverNamesRotated = false;
};

Client::Client(ClientOptions options, QObject *parent)
    : QObject(parent)
    , m_options(std::move(options))
    , m_bridge(this)
    , m_stdout(stdout)
    , m_stderr(stderr)
{
    m_retryTimer.setSingleShot(true);
    m_detachTimer.setSingleShot(true);

    connect(&m_bridge, &qt_spy::BridgeClient::socketConnected, this, &Client::onConnected);
    connect(&m_bridge, &qt_spy::BridgeClient::socketDisconnected, this, &Client::onDisconnected);
    connect(&m_bridge,
            &qt_spy::BridgeClient::socketError,
            this,
            &Client::onSocketError);
    connect(&m_bridge, &qt_spy::BridgeClient::helloReceived, this, &Client::handleHello);
    connect(&m_bridge, &qt_spy::BridgeClient::snapshotReceived, this, &Client::handleSnapshot);
    connect(&m_bridge,
            &qt_spy::BridgeClient::propertiesReceived,
            this,
            &Client::handlePropertiesMessage);
    connect(&m_bridge,
            &qt_spy::BridgeClient::selectionAckReceived,
            this,
            &Client::handleSelectionAck);
    connect(&m_bridge, &qt_spy::BridgeClient::nodeAdded, this, &Client::handleGenericMessage);
    connect(&m_bridge, &qt_spy::BridgeClient::nodeRemoved, this, &Client::handleGenericMessage);
    connect(&m_bridge,
            &qt_spy::BridgeClient::propertiesChanged,
            this,
            &Client::handleGenericMessage);
    connect(&m_bridge, &qt_spy::BridgeClient::errorReceived, this, &Client::handleErrorMessage);
    connect(&m_bridge,
            &qt_spy::BridgeClient::genericMessageReceived,
            this,
            &Client::handleGenericMessage);
    connect(&m_bridge, &qt_spy::BridgeClient::goodbyeReceived, this, &Client::handleGoodbye);
    connect(&m_retryTimer, &QTimer::timeout, this, &Client::retryTimeout);
    connect(&m_detachTimer, &QTimer::timeout, this, &Client::handleDetachTimeout);
}

void Client::start()
{
    connectToServer();
}

void Client::connectToServer()
{
    if (m_exiting) {
        return;
    }

    if (m_bridge.state() != QLocalSocket::UnconnectedState) {
        return;
    }

    const QString name = currentServerName();
    if (name.isEmpty()) {
        m_stderr << "qt-spy cli: no server names available; aborting." << Qt::endl;
        exitWithCode(EXIT_FAILURE);
        return;
    }

    m_stderr << "qt-spy cli: connecting to '" << name << "'..." << Qt::endl;
    m_bridge.connectToServer(name);
}

QString Client::currentServerName() const
{
    if (m_options.serverNames.isEmpty()) {
        return {};
    }
    return m_options.serverNames.first();
}

void Client::onConnected()
{
    m_stderr << "qt-spy cli: connected." << Qt::endl;
    m_retryTimer.stop();
    m_retryAttempt = 0;
    m_attachSent = false;
    m_attached = false;
    m_serverNamesRotated = false;

    sendAttach();
}

void Client::onDisconnected()
{
    if (m_exiting) {
        m_detachTimer.stop();
        m_detachRequested = false;
        m_pendingDetachRequestId.clear();
        finalizeExit();
        return;
    }

    m_stderr << "qt-spy cli: disconnected from server." << Qt::endl;
    resetConnectionState();
    scheduleReconnect();
}

void Client::onSocketError(QLocalSocket::LocalSocketError error, const QString &message)
{
    if (m_exiting) {
        return;
    }

    if (message.isEmpty()) {
        m_stderr << "qt-spy cli: socket error (" << error << ")" << Qt::endl;
    } else {
        m_stderr << "qt-spy cli: socket error: " << message << Qt::endl;
    }

    const bool invalidName = message.contains(QStringLiteral("Invalid name"), Qt::CaseInsensitive);
    if (invalidName) {
        if (advanceServerNameForRetry(true)) {
            return;
        }
    }

    switch (error) {
    case QLocalSocket::ConnectionRefusedError:
    case QLocalSocket::ServerNotFoundError:
    case QLocalSocket::PeerClosedError:
        if (!m_injectionAttempted && attemptInjection()) {
            QTimer::singleShot(200, this, &Client::connectToServer);
            return;
        }
        if (m_injectionAttempted && advanceServerNameForRetry()) {
            return;
        }
        scheduleReconnect();
        break;
    default:
        exitWithCode(EXIT_FAILURE);
        break;
    }
}

void Client::retryTimeout()
{
    if (m_exiting) {
        return;
    }

    if (m_options.maxRetries >= 0 && m_retryAttempt >= m_options.maxRetries) {
        m_stderr << "qt-spy cli: exceeded retry limit." << Qt::endl;
        exitWithCode(EXIT_FAILURE);
        return;
    }

    ++m_retryAttempt;
    connectToServer();
}

void Client::sendAttach()
{
    if (m_attachSent) {
        return;
    }

    m_bridge.sendAttach(QCoreApplication::applicationName(), qt_spy::protocol::kVersion);
    m_attachSent = true;
}

void Client::sendSnapshotRequest()
{
    m_bridge.requestSnapshot(nextRequestId());
}

void Client::requestProperties(const QString &id)
{
    if (id.isEmpty()) {
        return;
    }

    m_bridge.requestProperties(id, nextRequestId());
}

void Client::sendSelect(const QString &id)
{
    if (id.isEmpty()) {
        return;
    }

    m_bridge.selectNode(id, nextRequestId());
}

void Client::handleHello(const QJsonObject &message)
{
    m_attached = true;

    m_stderr << "qt-spy cli: handshake complete. app='"
             << message.value(QLatin1String(protocol::keys::kApplicationName)).toString()
             << "' pid=" << message.value(QLatin1String(protocol::keys::kApplicationPid)).toInt()
             << Qt::endl;

    sendSnapshotRequest();

    if (m_options.selectTarget.pending() && m_options.selectTarget.kind == ActionTarget::Kind::Id) {
        sendSelect(m_options.selectTarget.value);
        completeTarget(m_options.selectTarget);
    }

    if (m_options.propertiesTarget.pending() &&
        m_options.propertiesTarget.kind == ActionTarget::Kind::Id) {
        requestProperties(m_options.propertiesTarget.value);
        completeTarget(m_options.propertiesTarget);
    }
}

void Client::handleSnapshot(const QJsonObject &message)
{
    m_stdout << "--- snapshot ---" << Qt::endl;
    m_stdout << QJsonDocument(message).toJson(QJsonDocument::Indented) << Qt::endl;

    const QJsonArray rootIds = message.value(QLatin1String(protocol::keys::kRootIds)).toArray();
    resolveDeferredTargets(rootIds);

    if (m_options.snapshotOnce) {
        exitWithCode(EXIT_SUCCESS);
    }
}

void Client::handlePropertiesMessage(const QJsonObject &message)
{
    const QString id = message.value(QLatin1String(protocol::keys::kId)).toString();
    const QJsonObject props = message.value(QLatin1String(protocol::keys::kProperties)).toObject();
    const QString requestId = message.value(QLatin1String(protocol::keys::kRequestId)).toString();

    m_stdout << "--- properties";
    if (!id.isEmpty()) {
        m_stdout << " (id=" << id << ")";
    }
    if (!requestId.isEmpty()) {
        m_stdout << " [req=" << requestId << "]";
    }
    m_stdout << " ---" << Qt::endl;
    m_stdout << QJsonDocument(props).toJson(QJsonDocument::Indented) << Qt::endl;
}

void Client::handleSelectionAck(const QJsonObject &message)
{
    const QString id = message.value(QLatin1String(protocol::keys::kId)).toString();
    const QString requestId = message.value(QLatin1String(protocol::keys::kRequestId)).toString();

    m_stderr << "qt-spy cli: selection acknowledged for id='"
             << (id.isEmpty() ? QStringLiteral("<unknown>") : id) << "'";
    if (!requestId.isEmpty()) {
        m_stderr << " (requestId=" << requestId << ")";
    }
    m_stderr << Qt::endl;
}

void Client::handleGenericMessage(const QJsonObject &message)
{
    const QString type = message.value(QLatin1String(protocol::keys::kType)).toString();
    m_stdout << "--- " << type << " ---" << Qt::endl;
    m_stdout << QJsonDocument(message).toJson(QJsonDocument::Indented) << Qt::endl;
}

void Client::handleErrorMessage(const QJsonObject &message)
{
    m_stderr << "qt-spy cli: helper error: "
             << message.value(QStringLiteral("code")).toString() << " - "
             << message.value(QStringLiteral("message")).toString() << Qt::endl;
    if (message.contains(QStringLiteral("context"))) {
        m_stderr << QJsonDocument(message.value(QStringLiteral("context")).toObject())
                          .toJson(QJsonDocument::Indented)
                 << Qt::endl;
    }
}

void Client::handleGoodbye(const QJsonObject &message)
{
    if (!m_exiting || !m_detachRequested) {
        return;
    }

    const QString requestId = message.value(QLatin1String(protocol::keys::kRequestId)).toString();
    if (!m_pendingDetachRequestId.isEmpty() && requestId != m_pendingDetachRequestId) {
        // Ignore goodbye for an earlier request if a newer one is still pending.
        return;
    }

    m_stderr << "qt-spy cli: helper confirmed detach";
    if (!requestId.isEmpty()) {
        m_stderr << " [req=" << requestId << "]";
    }
    m_stderr << Qt::endl;

    m_detachTimer.stop();
    m_detachRequested = false;
    m_pendingDetachRequestId.clear();
    finalizeExit();
}

void Client::scheduleReconnect()
{
    if (m_exiting) {
        return;
    }

    if (m_bridge.state() != QLocalSocket::UnconnectedState) {
        m_bridge.disconnectFromServer();
    }

    int delayMs = 500 * std::min(5, std::max(1, m_retryAttempt + 1));
    m_stderr << "qt-spy cli: retrying in " << delayMs << " ms" << Qt::endl;
    m_retryTimer.start(delayMs);
    m_serverNamesRotated = false;
}

void Client::resetConnectionState()
{
    m_attachSent = false;
    m_attached = false;
    m_options.selectTarget.resetForReconnect();
    m_options.propertiesTarget.resetForReconnect();
    if (!m_exiting) {
        m_detachTimer.stop();
        m_detachRequested = false;
        m_pendingDetachRequestId.clear();
    }
}

bool Client::advanceServerNameForRetry(bool dropCurrent)
{
    if (m_options.serverNames.size() <= 1) {
        return false;
    }

    QString failedName = m_options.serverNames.takeFirst();
    if (!dropCurrent) {
        if (m_serverNamesRotated) {
            m_options.serverNames.prepend(failedName);
            return false;
        }
        m_options.serverNames.append(failedName);
        m_serverNamesRotated = true;
    } else {
        m_serverNamesRotated = false;
    }

    const QString nextName = currentServerName();
    m_stderr << "qt-spy cli: trying alternate server name '" << nextName << "'";
    if (dropCurrent) {
        m_stderr << " (discarded '" << failedName << "')";
    } else {
        m_stderr << " (queued '" << failedName << "' for later)";
    }
    m_stderr << "." << Qt::endl;

    if (m_bridge.state() != QLocalSocket::UnconnectedState) {
        m_bridge.disconnectFromServer();
    }
    resetConnectionState();
    QTimer::singleShot(0, this, &Client::connectToServer);
    return true;
}

bool Client::attemptInjection()
{
#if defined(Q_OS_UNIX)
    if (!m_options.enableInjection || m_injectionAttempted) {
        return false;
    }

    m_injectionAttempted = true;

    if (m_options.targetPid <= 0) {
        m_stderr << "qt-spy cli: unable to inject probe without a PID." << Qt::endl;
        return false;
    }

    // Use the proven shell script injection method (same as GUI inspector)
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString projectRoot = QDir(appDir).absoluteFilePath("../..");
    const QString injectionScript = projectRoot + "/scripts/inject_qt_spy.sh";
    
    if (!QFile::exists(injectionScript)) {
        m_stderr << "qt-spy cli: injection script not found at " << injectionScript << Qt::endl;
        return false;
    }

    QProcess injectionProcess;
    injectionProcess.setProgram(injectionScript);
    injectionProcess.setArguments({QString::number(m_options.targetPid)});
    injectionProcess.setWorkingDirectory(projectRoot);
    
    injectionProcess.start();
    
    if (!injectionProcess.waitForFinished(30000)) { // 30 second timeout
        m_stderr << "qt-spy cli: probe injection timed out for PID " << m_options.targetPid << Qt::endl;
        injectionProcess.kill();
        injectionProcess.waitForFinished(2000);
        return false;
    }
    
    const int exitCode = injectionProcess.exitCode();
    
    if (exitCode == 0) {
        m_injectionSucceeded = true;
        m_retryAttempt = 0;
        m_stderr << "qt-spy cli: injected probe into pid=" << m_options.targetPid << Qt::endl;
        return true;
    } else {
        const QByteArray stderrOutput = injectionProcess.readAllStandardError();
        if (!stderrOutput.isEmpty()) {
            m_stderr << "qt-spy cli: injection error: " << QString::fromLocal8Bit(stderrOutput).trimmed() << Qt::endl;
        }
        m_stderr << "qt-spy cli: probe injection failed for PID " << m_options.targetPid 
                 << " (exit code: " << exitCode << ")" << Qt::endl;
        return false;
    }
#else
    Q_UNUSED(m_options);
    m_injectionAttempted = true;
    m_stderr << "qt-spy cli: automatic probe injection is not supported on this platform." << Qt::endl;
    return false;
#endif
}



QString Client::nextRequestId()
{
    return QStringLiteral("req_%1").arg(++m_requestCounter);
}

void Client::resolveDeferredTargets(const QJsonArray &rootIds)
{
    auto resolveRoot = [&rootIds]() -> QString {
        if (rootIds.isEmpty()) {
            return {};
        }
        return rootIds.first().toString();
    };

    if (m_options.selectTarget.pending() &&
        m_options.selectTarget.kind == ActionTarget::Kind::FirstRoot) {
        const QString id = resolveRoot();
        if (id.isEmpty()) {
            m_stderr << "qt-spy cli: no root nodes available for selection." << Qt::endl;
        } else {
            sendSelect(id);
            completeTarget(m_options.selectTarget);
        }
    }

    if (m_options.propertiesTarget.pending() &&
        m_options.propertiesTarget.kind == ActionTarget::Kind::FirstRoot) {
        const QString id = resolveRoot();
        if (id.isEmpty()) {
            m_stderr << "qt-spy cli: no root nodes available for property request." << Qt::endl;
        } else {
            requestProperties(id);
            completeTarget(m_options.propertiesTarget);
        }
    }
}

void Client::completeTarget(ActionTarget &target)
{
    target.markCompleted();
}

void Client::exitWithCode(int code)
{
    if (m_exiting) {
        m_exitCode = code;
        return;
    }
    m_exiting = true;
    m_exitCode = code;
    m_retryTimer.stop();
    if (m_bridge.state() == QLocalSocket::ConnectedState && m_attached) {
        beginDetachHandshake();
        return;
    }
    finalizeExit();
}

void Client::beginDetachHandshake()
{
    if (m_detachRequested) {
        return;
    }

    m_detachRequested = true;
    m_pendingDetachRequestId = nextRequestId();
    m_stderr << "qt-spy cli: requesting helper detach";
    if (!m_pendingDetachRequestId.isEmpty()) {
        m_stderr << " [req=" << m_pendingDetachRequestId << "]";
    }
    m_stderr << Qt::endl;

    m_bridge.sendDetach(m_pendingDetachRequestId);
    m_detachTimer.start(2000);
}

void Client::handleDetachTimeout()
{
    if (!m_exiting || !m_detachRequested) {
        return;
    }

    m_stderr << "qt-spy cli: timeout waiting for helper goodbye; forcing disconnect." << Qt::endl;
    m_detachRequested = false;
    m_pendingDetachRequestId.clear();
    finalizeExit();
}

void Client::finalizeExit()
{
    if (m_exitSignalled) {
        return;
    }

    m_exitSignalled = true;
    m_detachTimer.stop();
    m_retryTimer.stop();
    if (m_bridge.state() == QLocalSocket::ConnectedState) {
        m_bridge.disconnectFromServer();
    }
    emit finished(m_exitCode);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qt_spy_cli"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("qt-spy inspector bridge CLI"));
    parser.addHelpOption();

    QCommandLineOption pidOption({QStringLiteral("p"), QStringLiteral("pid")},
                                 QStringLiteral("PID of the running Qt MMI to attach to."),
                                 QStringLiteral("pid"));
    parser.addOption(pidOption);

    QCommandLineOption serverOption({QStringLiteral("s"), QStringLiteral("server")},
                                    QStringLiteral("Explicit server name to connect to."),
                                    QStringLiteral("name"));
    parser.addOption(serverOption);

    // User-friendly attachment options
    QCommandLineOption listOption(QStringLiteral("list"),
                                  QStringLiteral("List available Qt processes and exit."));
    parser.addOption(listOption);

    QCommandLineOption autoOption({QStringLiteral("a"), QStringLiteral("auto")},
                                  QStringLiteral("Automatically attach to the most recent Qt process."));
    parser.addOption(autoOption);

    QCommandLineOption nameOption({QStringLiteral("n"), QStringLiteral("name")},
                                  QStringLiteral("Attach to process by name (e.g., 'rmmi', 'myapp')."),
                                  QStringLiteral("process_name"));
    parser.addOption(nameOption);

    QCommandLineOption titleOption({QStringLiteral("t"), QStringLiteral("title")},
                                   QStringLiteral("Attach to process by window title."),
                                   QStringLiteral("window_title"));
    parser.addOption(titleOption);

    QCommandLineOption interactiveOption({QStringLiteral("i"), QStringLiteral("interactive")},
                                         QStringLiteral("Show interactive process selection menu."));
    parser.addOption(interactiveOption);

    QCommandLineOption retriesOption(QStringLiteral("retries"),
                                     QStringLiteral("Number of reconnect attempts (-1 for infinite)."),
                                     QStringLiteral("count"),
                                     QStringLiteral("-1"));
    parser.addOption(retriesOption);

    QCommandLineOption snapshotOnceOption(QStringLiteral("snapshot-once"),
                                          QStringLiteral("Exit after the first snapshot is printed."));
    parser.addOption(snapshotOnceOption);

    QCommandLineOption selectOption(QStringLiteral("select"),
                                    QStringLiteral("Send a selectNode request (use an id or 'first-root')."),
                                    QStringLiteral("id"));
    parser.addOption(selectOption);

    QCommandLineOption propsOption(QStringLiteral("properties"),
                                   QStringLiteral("Request properties (use an id or 'first-root')."),
                                   QStringLiteral("id"));
    parser.addOption(propsOption);

    QCommandLineOption noInjectOption(QStringLiteral("no-inject"),
                                      QStringLiteral("Disable automatic probe injection."));
    parser.addOption(noInjectOption);

    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);
    
    const ResolvedServerName resolved = resolveServerNameEnhanced(
        parser, serverOption, pidOption, listOption, autoOption, 
        nameOption, titleOption, interactiveOption, out, err);
    if (resolved.names.isEmpty()) {
        err << resolved.error << Qt::endl;
        return EXIT_FAILURE;
    }

    const int maxRetries = parser.value(retriesOption).toInt();

    auto parseTarget = [](const QString &value) -> ActionTarget {
        ActionTarget target;
        if (value.compare(QStringLiteral("first-root"), Qt::CaseInsensitive) == 0) {
            target.kind = ActionTarget::Kind::FirstRoot;
        } else if (!value.isEmpty()) {
            target.kind = ActionTarget::Kind::Id;
            target.value = value;
        }
        if (target.kind != ActionTarget::Kind::None) {
            target.sticky = true;
        }
        return target;
    };

    ClientOptions options;
    options.serverNames = resolved.names;
    options.maxRetries = maxRetries;
    options.snapshotOnce = parser.isSet(snapshotOnceOption);
    options.selectTarget = parseTarget(parser.value(selectOption));
    options.propertiesTarget = parseTarget(parser.value(propsOption));
    options.targetPid = resolved.pid;
    options.enableInjection = !parser.isSet(noInjectOption);

    if (options.serverNames.size() > 1) {
        QTextStream(stderr) << "qt-spy cli: server name candidates: "
                            << options.serverNames.join(QStringLiteral(", ")) << Qt::endl;
    }

    auto *client = new Client(options);
    QObject::connect(client, &Client::finished, &app, [&app, client](int exitCode) {
        client->deleteLater();
        QMetaObject::invokeMethod(&app, [exitCode]() { QCoreApplication::exit(exitCode); },
                                  Qt::QueuedConnection);
    });
    
    // Connect app quit to client graceful shutdown
    QObject::connect(&app, &QCoreApplication::aboutToQuit, client, &Client::requestGracefulShutdown);

    // Setup signal handlers for graceful shutdown
    signal(SIGINT, [](int) {
        QTextStream(stderr) << "\nqt-spy cli: received SIGINT, disconnecting gracefully..." << Qt::endl;
        QMetaObject::invokeMethod(QCoreApplication::instance(), []() {
            QCoreApplication::quit();
        }, Qt::QueuedConnection);
    });
    
    signal(SIGTERM, [](int) {
        QTextStream(stderr) << "\nqt-spy cli: received SIGTERM, disconnecting gracefully..." << Qt::endl;
        QMetaObject::invokeMethod(QCoreApplication::instance(), []() {
            QCoreApplication::quit();
        }, Qt::QueuedConnection);
    });

    QTimer::singleShot(0, client, &Client::start);

    return app.exec();
}

#include "main.moc"
