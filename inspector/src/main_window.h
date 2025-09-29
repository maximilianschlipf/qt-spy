#pragma once

#include <QMainWindow>

QT_BEGIN_NAMESPACE
class QSplitter;
class QStatusBar;
class QAction;
class QToolBar;
class QLabel;
QT_END_NAMESPACE

namespace qt_spy {

class ConnectionManager;
class HierarchyTreeView;
class HierarchyTreeModel;
class PropertyGridWidget;
class ProcessSelectionDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget *parent = nullptr);
    
protected:
    void closeEvent(QCloseEvent *event) override;
    
private slots:
    void onAttachClicked();
    void onDetachClicked();
    void onRefreshClicked();
    void onConnectionStateChanged();
    void onStatusChanged(const QString &status);
    void onAttached(const QString &applicationName, qint64 pid);
    void onDetached();
    void onConnectionError(const QString &error);
    void onNodeSelected(const QString &nodeId);
    void onSnapshotReceived(const QJsonObject &snapshot);
    
private:
    void setupUi();
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupConnections();
    void updateActions();
    void showProcessSelectionDialog();
    
    ConnectionManager *m_connectionManager;
    HierarchyTreeView *m_treeView;
    HierarchyTreeModel *m_treeModel;
    PropertyGridWidget *m_propertyGrid;
    ProcessSelectionDialog *m_processDialog;
    
    // UI elements
    QSplitter *m_splitter;
    QToolBar *m_toolBar;
    QStatusBar *m_statusBar;
    QLabel *m_statusLabel;
    QLabel *m_connectionLabel;
    
    // Actions
    QAction *m_attachAction;
    QAction *m_detachAction;
    QAction *m_refreshAction;
    QAction *m_exitAction;
    QAction *m_aboutAction;
};

} // namespace qt_spy
