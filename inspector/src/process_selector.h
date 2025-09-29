#pragma once

#include "node_data.h"

#include <QObject>
#include <QDialog>
#include <QVector>
#include <QAbstractListModel>

QT_BEGIN_NAMESPACE
class QListView;
class QPushButton;
class QLabel;
QT_END_NAMESPACE

namespace qt_spy {

class ProcessSelector : public QObject {
    Q_OBJECT
    
public:
    explicit ProcessSelector(QObject *parent = nullptr);
    
    QVector<QtProcessInfo> discoverQtProcesses();
    QtProcessInfo findProcessByName(const QString &name);
    QtProcessInfo findProcessByPid(qint64 pid);
    
private:
    bool checkForQtLibraries(qint64 pid);
    bool checkForExistingProbe(qint64 pid);
    QString detectProcessName(qint64 pid);
};

class ProcessListModel : public QAbstractListModel {
    Q_OBJECT
    
public:
    explicit ProcessListModel(QObject *parent = nullptr);
    
    void setProcesses(const QVector<QtProcessInfo> &processes);
    QtProcessInfo processAt(int index) const;
    
    // QAbstractListModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    
private:
    QVector<QtProcessInfo> m_processes;
};

class ProcessSelectionDialog : public QDialog {
    Q_OBJECT
    
public:
    explicit ProcessSelectionDialog(QWidget *parent = nullptr);
    
    QtProcessInfo selectedProcess() const;
    void refreshProcessList();
    
public slots:
    void accept() override;
    
private slots:
    void onRefreshClicked();
    void onSelectionChanged();
    
private:
    void setupUi();
    void updateConnectButton();
    
    ProcessSelector *m_selector;
    ProcessListModel *m_model;
    QListView *m_listView;
    QPushButton *m_refreshButton;
    QPushButton *m_connectButton;
    QPushButton *m_cancelButton;
    QLabel *m_statusLabel;
    
    QtProcessInfo m_selectedProcess;
};

} // namespace qt_spy
