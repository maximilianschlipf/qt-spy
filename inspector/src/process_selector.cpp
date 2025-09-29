#include "process_selector.h"
#include "qt_spy/probe.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListView>
#include <QPushButton>
#include <QLabel>
#include <QProcess>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QLocalSocket>
#include <QStringList>


#include <algorithm>

namespace qt_spy {

ProcessSelector::ProcessSelector(QObject *parent)
    : QObject(parent)
{
}

QVector<QtProcessInfo> ProcessSelector::discoverQtProcesses() {
    QVector<QtProcessInfo> qtProcesses;
    
#if defined(Q_OS_UNIX)
    QProcess ps;
    ps.start(QStringLiteral("ps"), {QStringLiteral("aux")});
    if (!ps.waitForFinished(5000)) {
        return qtProcesses;
    }
    
    const QByteArray output = ps.readAllStandardOutput();
    const QStringList lines = QString::fromLocal8Bit(output).split('\n', Qt::SkipEmptyParts);
    
    for (int i = 1; i < lines.size(); ++i) { // Skip header line
        const QString line = lines.at(i);
        const QStringList fields = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        
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

QtProcessInfo ProcessSelector::findProcessByName(const QString &name) {
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

QtProcessInfo ProcessSelector::findProcessByPid(qint64 pid) {
    const QVector<QtProcessInfo> processes = discoverQtProcesses();
    
    for (const QtProcessInfo &process : processes) {
        if (process.pid == pid) {
            return process;
        }
    }
    
    return QtProcessInfo{};
}

bool ProcessSelector::checkForQtLibraries(qint64 pid) {
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
           mapsContent.contains("Qt6Qml", Qt::CaseInsensitive);
#else
    Q_UNUSED(pid);
    return false;
#endif
}

bool ProcessSelector::checkForExistingProbe(qint64 pid) {
    // Try to connect to potential qt-spy server names for this process
    const QString processName = detectProcessName(pid);
    
    // Generate comprehensive list of possible server names
    QStringList serverNames;
    
    // Standard names
    serverNames << qt_spy::defaultServerName(processName, pid);
    serverNames << qt_spy::defaultServerName(QString(), pid);
    
    // Additional patterns that might be generated by different process name detection methods
    if (!processName.isEmpty()) {
        // Try with full process name as-is
        serverNames << QString("qt_spy_%1_%2").arg(processName).arg(pid);
        
        // Try with basename extraction
        const QString baseName = QFileInfo(processName).baseName();
        if (baseName != processName) {
            serverNames << QString("qt_spy_%1_%2").arg(baseName).arg(pid);
        }
    }
    
    // Also check for existing sockets in /tmp matching the PID pattern
    QDir tmpDir("/tmp");
    const QStringList socketFilters = {
        QString("qt_spy_*_%1").arg(pid),
        QString("qt_spy_%1").arg(pid)
    };
    
    const QStringList existingSockets = tmpDir.entryList(socketFilters, QDir::System | QDir::Hidden);
    for (const QString &socketFile : existingSockets) {
        if (!serverNames.contains(socketFile)) {
            serverNames << socketFile;
        }
    }
    

    
    for (const QString &serverName : serverNames) {
        if (serverName.isEmpty()) continue;
        
        QLocalSocket testSocket;
        testSocket.connectToServer(serverName);
        if (testSocket.waitForConnected(200)) { // Increased timeout slightly

            testSocket.disconnectFromServer();
            return true;
        }
    }
    

    return false;
}

QString ProcessSelector::detectProcessName(qint64 pid) {
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

// ProcessListModel implementation

ProcessListModel::ProcessListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void ProcessListModel::setProcesses(const QVector<QtProcessInfo> &processes) {
    beginResetModel();
    m_processes = processes;
    endResetModel();
}

QtProcessInfo ProcessListModel::processAt(int index) const {
    if (index >= 0 && index < m_processes.size()) {
        return m_processes.at(index);
    }
    return QtProcessInfo{};
}

int ProcessListModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return m_processes.size();
}

QVariant ProcessListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_processes.size()) {
        return QVariant();
    }
    
    const QtProcessInfo &process = m_processes.at(index.row());
    
    switch (role) {
    case Qt::DisplayRole: {
        QString display = QString("[%1] %2 (PID: %3)")
                         .arg(index.row() + 1)
                         .arg(process.displayName())
                         .arg(process.pid);
        
        if (process.hasExistingProbe) {
            display += " [probe active]";
        }
        return display;
    }
    case Qt::ToolTipRole:
        return QString("Process: %1\nPID: %2\nCommand: %3")
               .arg(process.name)
               .arg(process.pid)
               .arg(process.commandLine);
    default:
        return QVariant();
    }
}

// ProcessSelectionDialog implementation

ProcessSelectionDialog::ProcessSelectionDialog(QWidget *parent)
    : QDialog(parent)
    , m_selector(new ProcessSelector(this))
    , m_model(new ProcessListModel(this))
{
    setupUi();
    refreshProcessList();
}

QtProcessInfo ProcessSelectionDialog::selectedProcess() const {
    return m_selectedProcess;
}

void ProcessSelectionDialog::refreshProcessList() {
    const QVector<QtProcessInfo> processes = m_selector->discoverQtProcesses();
    m_model->setProcesses(processes);
    
    if (processes.isEmpty()) {
        m_statusLabel->setText("No Qt processes found.");
    } else {
        m_statusLabel->setText(QString("Found %1 Qt process(es).").arg(processes.size()));
    }
    
    updateConnectButton();
}

void ProcessSelectionDialog::accept() {
    const QModelIndexList selection = m_listView->selectionModel()->selectedIndexes();
    if (!selection.isEmpty()) {
        const int row = selection.first().row();
        m_selectedProcess = m_model->processAt(row);
    }
    
    if (m_selectedProcess.pid > 0) {
        QDialog::accept();
    }
}

void ProcessSelectionDialog::onRefreshClicked() {
    refreshProcessList();
}

void ProcessSelectionDialog::onSelectionChanged() {
    updateConnectButton();
}

void ProcessSelectionDialog::setupUi() {
    setWindowTitle("Attach to Qt Process");
    setModal(true);
    resize(600, 400);
    
    auto *layout = new QVBoxLayout(this);
    
    // Status label
    m_statusLabel = new QLabel("Discovering Qt processes...", this);
    layout->addWidget(m_statusLabel);
    
    // Process list
    m_listView = new QListView(this);
    m_listView->setModel(m_model);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_listView);
    
    // Button layout
    auto *buttonLayout = new QHBoxLayout;
    
    m_refreshButton = new QPushButton("Refresh", this);
    buttonLayout->addWidget(m_refreshButton);
    
    buttonLayout->addStretch();
    
    m_cancelButton = new QPushButton("Cancel", this);
    buttonLayout->addWidget(m_cancelButton);
    
    m_connectButton = new QPushButton("Connect", this);
    m_connectButton->setDefault(true);
    m_connectButton->setEnabled(false);
    buttonLayout->addWidget(m_connectButton);
    
    layout->addLayout(buttonLayout);
    
    // Connections
    connect(m_refreshButton, &QPushButton::clicked, this, &ProcessSelectionDialog::onRefreshClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_connectButton, &QPushButton::clicked, this, &ProcessSelectionDialog::accept);
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ProcessSelectionDialog::onSelectionChanged);
    
    // Double-click to connect
    connect(m_listView, &QListView::doubleClicked, this, &ProcessSelectionDialog::accept);
}

void ProcessSelectionDialog::updateConnectButton() {
    const bool hasSelection = m_listView->selectionModel()->hasSelection();
    m_connectButton->setEnabled(hasSelection);
}

} // namespace qt_spy
