#pragma once

#include "node_data.h"

#include <QAbstractItemModel>
#include <QTreeView>
#include <QHash>
#include <QJsonObject>
#include <QItemSelection>

namespace qt_spy {

class BridgeClient;

class HierarchyTreeModel : public QAbstractItemModel {
    Q_OBJECT
    
public:
    explicit HierarchyTreeModel(QObject *parent = nullptr);
    
    void setBridgeClient(BridgeClient *bridge);
    void loadSnapshot(const QJsonObject &snapshot);
    void addNode(const QJsonObject &nodeData);
    void removeNode(const QString &nodeId);
    void updateNodeProperties(const QJsonObject &propertiesData);
    
    NodeData nodeData(const QModelIndex &index) const;
    QString nodeId(const QModelIndex &index) const;
    QModelIndex findNodeIndex(const QString &nodeId) const;
    
    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;
    
private slots:
    void onPropertiesReceived(const QJsonObject &message);
    
private:
    struct TreeItem {
        QString id;
        NodeData data;
        TreeItem *parent = nullptr;
        QVector<TreeItem *> children;
        bool childrenRequested = false;
        
        ~TreeItem() {
            qDeleteAll(children);
        }
        
        TreeItem *findChild(const QString &childId) const {
            for (TreeItem *child : children) {
                if (child->id == childId) {
                    return child;
                }
            }
            return nullptr;
        }
        
        int childIndex(TreeItem *child) const {
            return children.indexOf(child);
        }
    };
    
    TreeItem *itemFromIndex(const QModelIndex &index) const;
    TreeItem *findItem(const QString &nodeId, TreeItem *root = nullptr) const;
    void addChildToItem(TreeItem *parentItem, const NodeData &nodeData);
    void removeChildFromItem(TreeItem *parentItem, const QString &childId);
    void requestPropertiesForItem(TreeItem *item);
    
    BridgeClient *m_bridge;
    TreeItem *m_rootItem;
    QHash<QString, TreeItem *> m_itemMap;
    QHash<QString, QJsonObject> m_nodesMap; // Full nodes data for lazy loading
    QStringList m_pendingRequests;
};

class HierarchyTreeView : public QTreeView {
    Q_OBJECT
    
public:
    explicit HierarchyTreeView(QWidget *parent = nullptr);
    
signals:
    void nodeSelected(const QString &nodeId);
    
protected:
    void selectionChanged(const QItemSelection &selected,
                         const QItemSelection &deselected) override;
};

} // namespace qt_spy
