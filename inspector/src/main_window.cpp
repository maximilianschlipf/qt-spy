#include "main_window.h"
#include "connection_manager.h"
#include "hierarchy_tree.h"
#include "property_grid.h"
#include "process_selector.h"
#include "qt_spy/bridge_client.h"
#include "qt_spy/protocol.h"

#include <QApplication>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QMenuBar>
#include <QAction>
#include <QLabel>
#include <QMessageBox>
#include <QCloseEvent>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>
#include <QTimer>


namespace qt_spy {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_connectionManager(new ConnectionManager(this))
    , m_treeModel(new HierarchyTreeModel(this))
    , m_treeView(new HierarchyTreeView(this))
    , m_propertyGrid(new PropertyGridWidget(this))
    , m_processDialog(nullptr)
{
    setupUi();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupConnections();
    updateActions();
    
    // Set initial window properties
    setWindowTitle("Qt Spy Inspector");
    resize(1000, 700);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // Gracefully disconnect if connected
    if (m_connectionManager->state() == ConnectionManager::Attached ||
        m_connectionManager->state() == ConnectionManager::Connected) {
        m_connectionManager->disconnect();
    }
    
    event->accept();
}

void MainWindow::onAttachClicked() {
    showProcessSelectionDialog();
}

void MainWindow::onDetachClicked() {
    m_connectionManager->disconnect();
}

void MainWindow::onRefreshClicked() {
    if (m_connectionManager->state() == ConnectionManager::Attached) {
        // Request a new snapshot
        const QString requestId = QString("refresh_req_%1").arg(QDateTime::currentMSecsSinceEpoch());
    
        m_connectionManager->bridgeClient()->requestSnapshot(requestId);
    }
}

void MainWindow::onConnectionStateChanged() {
    updateActions();
}

void MainWindow::onStatusChanged(const QString &status) {
    m_statusLabel->setText(status);
}

void MainWindow::onAttached(const QString &applicationName, qint64 pid) {
    m_connectionLabel->setText(QString("Connected to: %1 (PID: %2)").arg(applicationName).arg(pid));
    
    // Request initial snapshot after successful attachment
    const QString requestId = QString("snapshot_req_%1").arg(QDateTime::currentMSecsSinceEpoch());
    
    // Add a small delay before requesting snapshot to ensure probe is ready
    QTimer::singleShot(500, [this, requestId]() {
        m_connectionManager->bridgeClient()->requestSnapshot(requestId);
    });
}

void MainWindow::onDetached() {
    m_connectionLabel->setText("Not connected");
    m_treeModel->loadSnapshot(QJsonObject()); // Clear tree
    m_propertyGrid->clearProperties();
}

void MainWindow::onConnectionError(const QString &error) {
    QMessageBox::warning(this, "Connection Error", 
                        QString("Failed to connect to Qt process:\n%1").arg(error));
}

void MainWindow::onNodeSelected(const QString &nodeId) {
    if (!nodeId.isEmpty()) {
        m_propertyGrid->showNodeProperties(nodeId);
        
        // Also send selection notification to the target app
        if (m_connectionManager->state() == ConnectionManager::Attached) {
            m_connectionManager->bridgeClient()->selectNode(nodeId);
        }
    }
}

void MainWindow::onSnapshotReceived(const QJsonObject &snapshot) {
    // Process the snapshot
    
    m_treeModel->loadSnapshot(snapshot);
    m_treeView->expandAll(); // Expand root level items initially
}

void MainWindow::setupUi() {
    // Create central splitter
    m_splitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(m_splitter);
    
    // Set up tree view
    m_treeView->setModel(m_treeModel);
    m_treeModel->setBridgeClient(m_connectionManager->bridgeClient());
    m_splitter->addWidget(m_treeView);
    
    // Set up property grid
    m_propertyGrid->setBridgeClient(m_connectionManager->bridgeClient());
    m_splitter->addWidget(m_propertyGrid);
    
    // Set initial splitter sizes (60% tree, 40% properties)
    m_splitter->setSizes({600, 400});
    m_splitter->setCollapsible(0, false);
    m_splitter->setCollapsible(1, false);
}

void MainWindow::setupMenuBar() {
    // File menu
    QMenu *fileMenu = menuBar()->addMenu("&File");
    
    m_attachAction = fileMenu->addAction("&Attach to Process...");
    m_attachAction->setShortcut(QKeySequence::Open);
    m_attachAction->setStatusTip("Attach to a Qt process");
    
    m_detachAction = fileMenu->addAction("&Detach");
    m_detachAction->setShortcut(QKeySequence("Ctrl+D"));
    m_detachAction->setStatusTip("Detach from current process");
    
    fileMenu->addSeparator();
    
    m_refreshAction = fileMenu->addAction("&Refresh");
    m_refreshAction->setShortcut(QKeySequence::Refresh);
    m_refreshAction->setStatusTip("Refresh the object hierarchy");
    
    fileMenu->addSeparator();
    
    m_exitAction = fileMenu->addAction("E&xit");
    m_exitAction->setShortcut(QKeySequence::Quit);
    m_exitAction->setStatusTip("Exit the application");
    
    // Help menu
    QMenu *helpMenu = menuBar()->addMenu("&Help");
    
    m_aboutAction = helpMenu->addAction("&About");
    m_aboutAction->setStatusTip("About Qt Spy Inspector");
}

void MainWindow::setupToolBar() {
    m_toolBar = addToolBar("Main");
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    
    m_toolBar->addAction(m_attachAction);
    m_toolBar->addAction(m_detachAction);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_refreshAction);
}

void MainWindow::setupStatusBar() {
    m_statusBar = statusBar();
    
    m_statusLabel = new QLabel("Ready", this);
    m_statusBar->addWidget(m_statusLabel);
    
    m_statusBar->addPermanentWidget(new QLabel(" | "));
    
    m_connectionLabel = new QLabel("Not connected", this);
    m_statusBar->addPermanentWidget(m_connectionLabel);
}

void MainWindow::setupConnections() {
    // Action connections
    connect(m_attachAction, &QAction::triggered, this, &MainWindow::onAttachClicked);
    connect(m_detachAction, &QAction::triggered, this, &MainWindow::onDetachClicked);
    connect(m_refreshAction, &QAction::triggered, this, &MainWindow::onRefreshClicked);
    connect(m_exitAction, &QAction::triggered, this, &QMainWindow::close);
    connect(m_aboutAction, &QAction::triggered, [this]() {
        QMessageBox::about(this, "About Qt Spy Inspector",
                          "Qt Spy Inspector\n\n"
                          "A tool for inspecting Qt object hierarchies and properties.\n\n"
                          "Phase 2 Implementation");
    });
    
    // Connection manager signals
    connect(m_connectionManager, &ConnectionManager::stateChanged,
            this, &MainWindow::onConnectionStateChanged);
    connect(m_connectionManager, &ConnectionManager::statusChanged,
            this, &MainWindow::onStatusChanged);
    connect(m_connectionManager, &ConnectionManager::attached,
            this, &MainWindow::onAttached);
    connect(m_connectionManager, &ConnectionManager::detached,
            this, &MainWindow::onDetached);
    connect(m_connectionManager, &ConnectionManager::connectionError,
            this, &MainWindow::onConnectionError);
    
    // Bridge client signals
    connect(m_connectionManager->bridgeClient(), &BridgeClient::snapshotReceived,
            this, &MainWindow::onSnapshotReceived);
    
    // Bridge client error handling
    connect(m_connectionManager->bridgeClient(), &BridgeClient::errorReceived, [this](const QJsonObject &msg) {
        statusBar()->showMessage("Error: " + msg.value("message").toString(), 5000);
    });
    
    // Bridge client debug signals
    connect(m_connectionManager->bridgeClient(), &BridgeClient::errorReceived, [](const QJsonObject &msg) {
        qDebug() << "MainWindow: Bridge error:" << msg.value("message").toString();
    });
    
    // Tree view signals
    connect(m_treeView, &HierarchyTreeView::nodeSelected,
            this, &MainWindow::onNodeSelected);
}

void MainWindow::updateActions() {
    const bool connected = (m_connectionManager->state() == ConnectionManager::Attached);
    const bool connecting = (m_connectionManager->state() == ConnectionManager::Connecting);
    
    m_attachAction->setEnabled(!connected && !connecting);
    m_detachAction->setEnabled(connected);
    m_refreshAction->setEnabled(connected);
}

void MainWindow::showProcessSelectionDialog() {
    if (!m_processDialog) {
        m_processDialog = new ProcessSelectionDialog(this);
    }
    
    // Refresh process list
    m_processDialog->refreshProcessList();
    
    if (m_processDialog->exec() == QDialog::Accepted) {
        const QtProcessInfo selectedProcess = m_processDialog->selectedProcess();
        if (selectedProcess.pid > 0) {
            m_connectionManager->connectToProcess(selectedProcess);
        }
    }
}

} // namespace qt_spy
