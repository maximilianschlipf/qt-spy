#pragma once

#include "node_data.h"

#include <QAbstractTableModel>
#include <QTableView>
#include <QJsonObject>
#include <QVector>

QT_BEGIN_NAMESPACE
class QHeaderView;
class QMenu;
QT_END_NAMESPACE

namespace qt_spy {

class BridgeClient;

class PropertyTableModel : public QAbstractTableModel {
    Q_OBJECT
    
public:
    explicit PropertyTableModel(QObject *parent = nullptr);
    
    void setProperties(const QJsonObject &properties);
    void setNodeInfo(const QString &nodeId, const QString &className, const QString &objectName);
    void clear();
    
    PropertyInfo propertyAt(int row) const;
    
    // QAbstractTableModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    
private:
    void parseProperties(const QJsonObject &properties);
    
    QString m_nodeId;
    QString m_className;
    QString m_objectName;
    QVector<PropertyInfo> m_properties;
};

class PropertyTableView : public QTableView {
    Q_OBJECT
    
public:
    explicit PropertyTableView(QWidget *parent = nullptr);
    
    void setBridgeClient(BridgeClient *bridge);
    void setCurrentNodeId(const QString &nodeId);
    
public slots:
    void refreshProperties();
    void copySelectedValue();
    void copySelectedRow();
    void copyAllProperties();
    
signals:
    void nodePropertiesRequested(const QString &nodeId);
    
protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    
private slots:
    void onPropertiesReceived(const QJsonObject &message);
    
private:
    void setupContextMenu();
    QString selectedPropertyValue() const;
    QString selectedPropertyRow() const;
    QString allPropertiesText() const;
    
    BridgeClient *m_bridge;
    QString m_currentNodeId;
    QMenu *m_contextMenu;
};

class PropertyGridWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit PropertyGridWidget(QWidget *parent = nullptr);
    
    void setBridgeClient(BridgeClient *bridge);
    PropertyTableModel *model() const { return m_model; }
    PropertyTableView *view() const { return m_view; }
    
public slots:
    void showNodeProperties(const QString &nodeId);
    void clearProperties();
    
private:
    void setupUi();
    
    PropertyTableModel *m_model;
    PropertyTableView *m_view;
};

} // namespace qt_spy
