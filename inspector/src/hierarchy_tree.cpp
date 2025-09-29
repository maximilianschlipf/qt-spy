#include "hierarchy_tree.h"
#include "qt_spy/bridge_client.h"
#include "qt_spy/protocol.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QHeaderView>
#include <QDateTime>
#include <QDebug>

namespace qt_spy {

HierarchyTreeModel::HierarchyTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
    , m_bridge(nullptr)
    , m_rootItem(new TreeItem)
{
}

void HierarchyTreeModel::setBridgeClient(BridgeClient *bridge) {
    if (m_bridge) {
        disconnect(m_bridge, nullptr, this, nullptr);
    }
    
    m_bridge = bridge;
    
    if (m_bridge) {
        connect(m_bridge, &BridgeClient::propertiesReceived, 
                this, &HierarchyTreeModel::onPropertiesReceived);
        connect(m_bridge, &BridgeClient::nodeAdded,
                this, [this](const QJsonObject &msg) { addNode(msg); });
        connect(m_bridge, &BridgeClient::nodeRemoved,
                this, [this](const QJsonObject &msg) { 
                    const QString nodeId = msg.value(QLatin1String(protocol::keys::kId)).toString();
                    removeNode(nodeId);
                });
        connect(m_bridge, &BridgeClient::propertiesChanged,
                this, &HierarchyTreeModel::updateNodeProperties);
    }
}

void HierarchyTreeModel::loadSnapshot(const QJsonObject &snapshot) {
    qDebug() << "HierarchyTreeModel: Loading snapshot with keys:" << snapshot.keys();
    
    beginResetModel();
    
    // Clear existing data
    delete m_rootItem;
    m_rootItem = new TreeItem;
    m_itemMap.clear();
    m_nodesMap.clear();
    
    // Parse root node IDs
    const QJsonArray rootIds = snapshot.value(QLatin1String(protocol::keys::kRootIds)).toArray();
    
    // Parse nodes - check if it's an array or object
    QJsonValue nodesValue = snapshot.value(QLatin1String(protocol::keys::kNodes));
    QHash<QString, QJsonObject> nodesMap;
    
    if (nodesValue.isArray()) {
        // Nodes sent as array - convert to map by ID
        const QJsonArray nodesArray = nodesValue.toArray();
        qDebug() << "HierarchyTreeModel: Found" << rootIds.size() << "root IDs and" << nodesArray.size() << "nodes (array format)";
        
        for (const QJsonValue &nodeValue : nodesArray) {
            const QJsonObject nodeObj = nodeValue.toObject();
            const QString nodeId = nodeObj.value(QLatin1String(protocol::keys::kId)).toString();
            if (!nodeId.isEmpty()) {
                nodesMap.insert(nodeId, nodeObj);
            }
        }
    } else {
        // Nodes sent as object (legacy format)
        const QJsonObject nodesObject = nodesValue.toObject();
        qDebug() << "HierarchyTreeModel: Found" << rootIds.size() << "root IDs and" << nodesObject.size() << "nodes (object format)";
        
        for (auto it = nodesObject.begin(); it != nodesObject.end(); ++it) {
            nodesMap.insert(it.key(), it.value().toObject());
        }
    }
    
    qDebug() << "HierarchyTreeModel: Created nodes map with" << nodesMap.size() << "entries";
    
    // Create root items
    for (const QJsonValue &rootIdValue : rootIds) {
        const QString rootId = rootIdValue.toString();
        if (rootId.isEmpty()) continue;
        
        qDebug() << "HierarchyTreeModel: Processing root ID:" << rootId;
        
        const QJsonObject nodeJson = nodesMap.value(rootId);
        if (nodeJson.isEmpty()) {
            qDebug() << "HierarchyTreeModel: No node data found for root ID:" << rootId;
            continue;
        }
        
        NodeData nodeData = NodeData::fromJson(nodeJson);
        nodeData.id = rootId;
        
        // Skip nodes with empty display names (internal Qt objects)
        const QString displayName = nodeData.displayName();
        if (displayName.isEmpty() || displayName.trimmed().isEmpty()) {
            qDebug() << "HierarchyTreeModel: Skipping root item with empty display name:" << rootId;
            continue;
        }
        
        qDebug() << "HierarchyTreeModel: Creating root item for:" << displayName;
        addChildToItem(m_rootItem, nodeData);
    }
    
    // Store the full nodes map for lazy loading of children
    m_nodesMap = nodesMap;
    
    qDebug() << "HierarchyTreeModel: Final tree has" << m_rootItem->children.size() << "root items";
    
    endResetModel();
}

void HierarchyTreeModel::addNode(const QJsonObject &nodeData) {
    const QString nodeId = nodeData.value(QLatin1String(protocol::keys::kId)).toString();
    const QString parentId = nodeData.value(QLatin1String(protocol::keys::kParentId)).toString();
    
    if (nodeId.isEmpty()) return;
    
    NodeData data = NodeData::fromJson(nodeData);
    data.id = nodeId;
    
    TreeItem *parentItem = parentId.isEmpty() ? m_rootItem : m_itemMap.value(parentId);
    if (!parentItem) {
        // Parent not found, might need to request it
        return;
    }
    
    // Check if node already exists
    if (m_itemMap.contains(nodeId)) {
        return;
    }
    
    const int row = parentItem->children.size();
    beginInsertRows(createIndex(parentItem->parent ? parentItem->parent->childIndex(parentItem) : 0, 0, parentItem), 
                    row, row);
    
    addChildToItem(parentItem, data);
    
    endInsertRows();
}

void HierarchyTreeModel::removeNode(const QString &nodeId) {
    TreeItem *item = m_itemMap.value(nodeId);
    if (!item || !item->parent) return;
    
    TreeItem *parent = item->parent;
    const int row = parent->childIndex(item);
    
    beginRemoveRows(createIndex(parent->parent ? parent->parent->childIndex(parent) : 0, 0, parent), 
                    row, row);
    
    removeChildFromItem(parent, nodeId);
    
    endRemoveRows();
}

void HierarchyTreeModel::updateNodeProperties(const QJsonObject &propertiesData) {
    const QString nodeId = propertiesData.value(QLatin1String(protocol::keys::kId)).toString();
    TreeItem *item = m_itemMap.value(nodeId);
    
    if (!item) return;
    
    // Update properties
    const QJsonObject properties = propertiesData.value(QLatin1String(protocol::keys::kProperties)).toObject();
    item->data.properties = properties;
    
    // Update display info if available
    const QString className = properties.value("className").toString();
    const QString objectName = properties.value("objectName").toString();
    
    if (!className.isEmpty()) {
        item->data.className = className;
    }
    if (!objectName.isEmpty()) {
        item->data.objectName = objectName;
    }
    
    // Emit data changed for this item
    const QModelIndex itemIndex = createIndex(item->parent->childIndex(item), 0, item);
    emit dataChanged(itemIndex, itemIndex);
}

NodeData HierarchyTreeModel::nodeData(const QModelIndex &index) const {
    TreeItem *item = itemFromIndex(index);
    return item ? item->data : NodeData{};
}

QString HierarchyTreeModel::nodeId(const QModelIndex &index) const {
    TreeItem *item = itemFromIndex(index);
    return item ? item->id : QString();
}

QModelIndex HierarchyTreeModel::findNodeIndex(const QString &nodeId) const {
    TreeItem *item = m_itemMap.value(nodeId);
    if (!item || !item->parent) {
        return QModelIndex();
    }
    
    return createIndex(item->parent->childIndex(item), 0, item);
}

QModelIndex HierarchyTreeModel::index(int row, int column, const QModelIndex &parent) const {
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }
    
    TreeItem *parentItem = itemFromIndex(parent);
    if (!parentItem) {
        parentItem = m_rootItem;
    }
    
    if (row < parentItem->children.size()) {
        return createIndex(row, column, parentItem->children.at(row));
    }
    
    return QModelIndex();
}

QModelIndex HierarchyTreeModel::parent(const QModelIndex &child) const {
    TreeItem *childItem = itemFromIndex(child);
    if (!childItem) {
        return QModelIndex();
    }
    
    TreeItem *parentItem = childItem->parent;
    if (!parentItem || parentItem == m_rootItem) {
        return QModelIndex();
    }
    
    TreeItem *grandParentItem = parentItem->parent;
    if (!grandParentItem) {
        return QModelIndex();
    }
    
    return createIndex(grandParentItem->childIndex(parentItem), 0, parentItem);
}

int HierarchyTreeModel::rowCount(const QModelIndex &parent) const {
    TreeItem *parentItem = itemFromIndex(parent);
    if (!parentItem) {
        parentItem = m_rootItem;
    }
    
    return parentItem->children.size();
}

int HierarchyTreeModel::columnCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return 1; // Single column for now
}

QVariant HierarchyTreeModel::data(const QModelIndex &index, int role) const {
    TreeItem *item = itemFromIndex(index);
    if (!item) {
        return QVariant();
    }
    
    switch (role) {
    case Qt::DisplayRole:
        return item->data.displayName();
    case Qt::ToolTipRole:
        return QString("ID: %1\nClass: %2\nObject Name: %3")
               .arg(item->id)
               .arg(item->data.className)
               .arg(item->data.objectName.isEmpty() ? "<unnamed>" : item->data.objectName);
    default:
        return QVariant();
    }
}

QVariant HierarchyTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case 0:
            return "Object Hierarchy";
        default:
            return QVariant();
        }
    }
    return QVariant();
}

bool HierarchyTreeModel::hasChildren(const QModelIndex &parent) const {
    TreeItem *parentItem = itemFromIndex(parent);
    if (!parentItem) {
        parentItem = m_rootItem;
    }
    
    // Root item always has children
    if (parentItem == m_rootItem) {
        return !parentItem->children.isEmpty();
    }
    
    // If children are already loaded, check if any exist
    if (!parentItem->children.isEmpty()) {
        return true;
    }
    
    // If children haven't been loaded yet, check the snapshot data
    if (!parentItem->childrenRequested) {
        const QJsonObject nodeData = m_nodesMap.value(parentItem->id);
        if (!nodeData.isEmpty()) {
            const QJsonArray childIds = nodeData.value(QLatin1String(protocol::keys::kChildIds)).toArray();
            return !childIds.isEmpty();
        }
    }
    
    return false;
}

bool HierarchyTreeModel::canFetchMore(const QModelIndex &parent) const {
    TreeItem *parentItem = itemFromIndex(parent);
    if (!parentItem || parentItem == m_rootItem) {
        return false;
    }
    
    // Can fetch more if we haven't loaded children yet and node has childIds in the snapshot
    if (parentItem->childrenRequested) {
        return false;
    }
    
    // Check if this node has children in the stored snapshot data
    const QJsonObject nodeData = m_nodesMap.value(parentItem->id);
    if (nodeData.isEmpty()) {
        return false;
    }
    
    const QJsonArray childIds = nodeData.value(QLatin1String(protocol::keys::kChildIds)).toArray();
    return !childIds.isEmpty();
}

void HierarchyTreeModel::fetchMore(const QModelIndex &parent) {
    TreeItem *parentItem = itemFromIndex(parent);
    if (!parentItem || !canFetchMore(parent)) {
        return;
    }
    
    // Load children from stored snapshot data
    const QJsonObject nodeData = m_nodesMap.value(parentItem->id);
    if (nodeData.isEmpty()) {
        return;
    }
    
    const QJsonArray childIds = nodeData.value(QLatin1String(protocol::keys::kChildIds)).toArray();
    if (childIds.isEmpty()) {
        parentItem->childrenRequested = true;
        return;
    }
    
    qDebug() << "HierarchyTreeModel: Loading" << childIds.size() << "children for" << parentItem->data.displayName();
    
    // Create child nodes from snapshot data
    QVector<NodeData> childrenData;
    for (const QJsonValue &childIdValue : childIds) {
        const QString childId = childIdValue.toString();
        if (childId.isEmpty() || m_itemMap.contains(childId)) {
            continue;
        }
        
        const QJsonObject childNodeData = m_nodesMap.value(childId);
        if (childNodeData.isEmpty()) {
            continue;
        }
        
        NodeData childData = NodeData::fromJson(childNodeData);
        childData.id = childId;
        
        // Skip children with empty display names
        if (childData.displayName().trimmed().isEmpty()) {
            qDebug() << "HierarchyTreeModel: Skipping child with empty display name:" << childId;
            continue;
        }
        
        childrenData.append(childData);
    }
    
    if (!childrenData.isEmpty()) {
        const int startRow = parentItem->children.size();
        const int endRow = startRow + childrenData.size() - 1;
        
        beginInsertRows(parent, startRow, endRow);
        
        for (const NodeData &childData : childrenData) {
            addChildToItem(parentItem, childData);
        }
        
        endInsertRows();
    }
    
    parentItem->childrenRequested = true;
    parentItem->data.childrenLoaded = true;
}

void HierarchyTreeModel::onPropertiesReceived(const QJsonObject &message) {
    const QString nodeId = message.value(QLatin1String(protocol::keys::kId)).toString();
    const QString requestId = message.value(QLatin1String(protocol::keys::kRequestId)).toString();
    
    // Remove from pending requests
    m_pendingRequests.removeAll(requestId);
    
    TreeItem *item = m_itemMap.value(nodeId);
    if (!item) return;
    
    // Update properties
    const QJsonObject properties = message.value(QLatin1String(protocol::keys::kProperties)).toObject();
    item->data.properties = properties;
    
    // Process children if available
    const QJsonArray childIds = properties.value(QLatin1String(protocol::keys::kChildIds)).toArray();
    if (!childIds.isEmpty()) {
        beginInsertRows(createIndex(item->parent->childIndex(item), 0, item), 0, childIds.size() - 1);
        
        for (const QJsonValue &childIdValue : childIds) {
            const QString childId = childIdValue.toString();
            if (childId.isEmpty() || m_itemMap.contains(childId)) continue;
            
            NodeData childData;
            childData.id = childId;
            childData.className = "Loading..."; // Placeholder
            
            addChildToItem(item, childData);
        }
        
        endInsertRows();
    }
    
    item->childrenRequested = true;
    item->data.childrenLoaded = true;
}

HierarchyTreeModel::TreeItem *HierarchyTreeModel::itemFromIndex(const QModelIndex &index) const {
    if (!index.isValid()) {
        return nullptr;
    }
    return static_cast<TreeItem *>(index.internalPointer());
}

HierarchyTreeModel::TreeItem *HierarchyTreeModel::findItem(const QString &nodeId, TreeItem *root) const {
    if (!root) {
        root = m_rootItem;
    }
    
    if (root->id == nodeId) {
        return root;
    }
    
    for (TreeItem *child : root->children) {
        if (TreeItem *found = findItem(nodeId, child)) {
            return found;
        }
    }
    
    return nullptr;
}

void HierarchyTreeModel::addChildToItem(TreeItem *parentItem, const NodeData &nodeData) {
    TreeItem *childItem = new TreeItem;
    childItem->id = nodeData.id;
    childItem->data = nodeData;
    childItem->parent = parentItem;
    
    parentItem->children.append(childItem);
    m_itemMap.insert(nodeData.id, childItem);
}

void HierarchyTreeModel::removeChildFromItem(TreeItem *parentItem, const QString &childId) {
    for (int i = 0; i < parentItem->children.size(); ++i) {
        TreeItem *child = parentItem->children.at(i);
        if (child->id == childId) {
            parentItem->children.removeAt(i);
            m_itemMap.remove(childId);
            delete child;
            break;
        }
    }
}

void HierarchyTreeModel::requestPropertiesForItem(TreeItem *item) {
    if (!m_bridge || item->childrenRequested) {
        return;
    }
    
    const QString requestId = QString("tree_req_%1").arg(QDateTime::currentMSecsSinceEpoch());
    m_pendingRequests.append(requestId);
    m_bridge->requestProperties(item->id, requestId);
    item->childrenRequested = true;
}

// HierarchyTreeView implementation

HierarchyTreeView::HierarchyTreeView(QWidget *parent)
    : QTreeView(parent)
{
    setUniformRowHeights(true);
    setAlternatingRowColors(true);
    setRootIsDecorated(true);
    setHeaderHidden(false);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    
    header()->setStretchLastSection(true);
}

void HierarchyTreeView::selectionChanged(const QItemSelection &selected,
                                        const QItemSelection &deselected) {
    QTreeView::selectionChanged(selected, deselected);
    
    const QModelIndexList indexes = selected.indexes();
    if (!indexes.isEmpty()) {
        if (auto *treeModel = qobject_cast<HierarchyTreeModel *>(model())) {
            const QString nodeId = treeModel->nodeId(indexes.first());
            if (!nodeId.isEmpty()) {
                emit nodeSelected(nodeId);
            }
        }
    }
}

} // namespace qt_spy
