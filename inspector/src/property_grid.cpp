#include "property_grid.h"
#include "qt_spy/bridge_client.h"
#include "qt_spy/protocol.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QJsonValue>
#include <QJsonDocument>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QLabel>
#include <QDateTime>

namespace qt_spy {

PropertyTableModel::PropertyTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void PropertyTableModel::setProperties(const QJsonObject &properties) {
    beginResetModel();
    parseProperties(properties);
    endResetModel();
}

void PropertyTableModel::setNodeInfo(const QString &nodeId, const QString &className, const QString &objectName) {
    m_nodeId = nodeId;
    m_className = className;
    m_objectName = objectName;
}

void PropertyTableModel::clear() {
    beginResetModel();
    m_properties.clear();
    m_nodeId.clear();
    m_className.clear();
    m_objectName.clear();
    endResetModel();
}

PropertyInfo PropertyTableModel::propertyAt(int row) const {
    if (row >= 0 && row < m_properties.size()) {
        return m_properties.at(row);
    }
    return PropertyInfo{};
}

int PropertyTableModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return m_properties.size();
}

int PropertyTableModel::columnCount(const QModelIndex &parent) const {
    Q_UNUSED(parent)
    return 2; // Property name and value
}

QVariant PropertyTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_properties.size()) {
        return QVariant();
    }
    
    const PropertyInfo &prop = m_properties.at(index.row());
    
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case 0:
            return prop.name;
        case 1:
            return prop.displayValue;
        default:
            return QVariant();
        }
    case Qt::ToolTipRole:
        switch (index.column()) {
        case 0:
            return QString("Property: %1\nType: %2").arg(prop.name).arg(prop.type);
        case 1:
            return QString("Value: %1\nType: %2").arg(prop.displayValue).arg(prop.type);
        default:
            return QVariant();
        }
    case Qt::UserRole: // Raw value for copying
        return prop.value;
    default:
        return QVariant();
    }
}

QVariant PropertyTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case 0:
            return "Property";
        case 1:
            return "Value";
        default:
            return QVariant();
        }
    }
    return QVariant();
}

void PropertyTableModel::parseProperties(const QJsonObject &properties) {
    m_properties.clear();
    
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        const QString name = it.key();
        const QJsonValue value = it.value();
        
        QVariant qtValue;
        QString type;
        
        switch (value.type()) {
        case QJsonValue::Bool:
            qtValue = value.toBool();
            type = "bool";
            break;
        case QJsonValue::Double:
            qtValue = value.toDouble();
            type = "number";
            break;
        case QJsonValue::String:
            qtValue = value.toString();
            type = "string";
            break;
        case QJsonValue::Array:
            qtValue = QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
            type = "array";
            break;
        case QJsonValue::Object:
            qtValue = QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
            type = "object";
            break;
        case QJsonValue::Null:
            qtValue = QVariant();
            type = "null";
            break;
        default:
            qtValue = value.toVariant();
            type = "unknown";
            break;
        }
        
        m_properties.append(PropertyInfo(name, qtValue, type));
    }
    
    // Sort properties by name for easier browsing
    std::sort(m_properties.begin(), m_properties.end(),
              [](const PropertyInfo &a, const PropertyInfo &b) {
                  return a.name < b.name;
              });
}

// PropertyTableView implementation

PropertyTableView::PropertyTableView(QWidget *parent)
    : QTableView(parent)
    , m_bridge(nullptr)
{
    setAlternatingRowColors(true);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSortingEnabled(false);
    
    horizontalHeader()->setStretchLastSection(true);
    horizontalHeader()->resizeSection(0, 150);
    verticalHeader()->setDefaultSectionSize(22);
    verticalHeader()->hide();
    
    setupContextMenu();
}

void PropertyTableView::setBridgeClient(BridgeClient *bridge) {
    if (m_bridge) {
        disconnect(m_bridge, nullptr, this, nullptr);
    }
    
    m_bridge = bridge;
    
    if (m_bridge) {
        connect(m_bridge, &BridgeClient::propertiesReceived,
                this, &PropertyTableView::onPropertiesReceived);
    }
}

void PropertyTableView::setCurrentNodeId(const QString &nodeId) {
    m_currentNodeId = nodeId;
}

void PropertyTableView::refreshProperties() {
    if (!m_currentNodeId.isEmpty()) {
        emit nodePropertiesRequested(m_currentNodeId);
    }
}

void PropertyTableView::copySelectedValue() {
    const QString text = selectedPropertyValue();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void PropertyTableView::copySelectedRow() {
    const QString text = selectedPropertyRow();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void PropertyTableView::copyAllProperties() {
    const QString text = allPropertiesText();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void PropertyTableView::contextMenuEvent(QContextMenuEvent *event) {
    if (m_contextMenu) {
        m_contextMenu->exec(event->globalPos());
    }
}

void PropertyTableView::onPropertiesReceived(const QJsonObject &message) {
    const QString nodeId = message.value(QLatin1String(protocol::keys::kId)).toString();
    
    if (nodeId != m_currentNodeId) {
        return; // Not for the currently selected node
    }
    
    if (auto *tableModel = qobject_cast<PropertyTableModel *>(model())) {
        const QJsonObject properties = message.value(QLatin1String(protocol::keys::kProperties)).toObject();
        
        // Extract node info from properties
        const QString className = properties.value("className").toString();
        const QString objectName = properties.value("objectName").toString();
        
        tableModel->setNodeInfo(nodeId, className, objectName);
        tableModel->setProperties(properties);
    }
}

void PropertyTableView::setupContextMenu() {
    m_contextMenu = new QMenu(this);
    
    QAction *copyValueAction = m_contextMenu->addAction("Copy Value");
    connect(copyValueAction, &QAction::triggered, this, &PropertyTableView::copySelectedValue);
    
    QAction *copyRowAction = m_contextMenu->addAction("Copy Property=Value");
    connect(copyRowAction, &QAction::triggered, this, &PropertyTableView::copySelectedRow);
    
    m_contextMenu->addSeparator();
    
    QAction *copyAllAction = m_contextMenu->addAction("Copy All Properties");
    connect(copyAllAction, &QAction::triggered, this, &PropertyTableView::copyAllProperties);
    
    m_contextMenu->addSeparator();
    
    QAction *refreshAction = m_contextMenu->addAction("Refresh");
    connect(refreshAction, &QAction::triggered, this, &PropertyTableView::refreshProperties);
}

QString PropertyTableView::selectedPropertyValue() const {
    const QModelIndexList selection = selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        return QString();
    }
    
    const QModelIndex valueIndex = model()->index(selection.first().row(), 1);
    return model()->data(valueIndex, Qt::DisplayRole).toString();
}

QString PropertyTableView::selectedPropertyRow() const {
    const QModelIndexList selection = selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        return QString();
    }
    
    const int row = selection.first().row();
    const QString property = model()->data(model()->index(row, 0), Qt::DisplayRole).toString();
    const QString value = model()->data(model()->index(row, 1), Qt::DisplayRole).toString();
    
    return QString("%1=%2").arg(property, value);
}

QString PropertyTableView::allPropertiesText() const {
    if (!model()) {
        return QString();
    }
    
    QStringList lines;
    
    if (auto *tableModel = qobject_cast<PropertyTableModel *>(model())) {
        lines << QString("# Properties for node: %1").arg(m_currentNodeId);
        lines << QString("# Generated: %1").arg(QDateTime::currentDateTime().toString());
        lines << "";
        
        for (int row = 0; row < model()->rowCount(); ++row) {
            const QString property = model()->data(model()->index(row, 0), Qt::DisplayRole).toString();
            const QString value = model()->data(model()->index(row, 1), Qt::DisplayRole).toString();
            lines << QString("%1=%2").arg(property, value);
        }
    }
    
    return lines.join('\n');
}

// PropertyGridWidget implementation

PropertyGridWidget::PropertyGridWidget(QWidget *parent)
    : QWidget(parent)
    , m_model(new PropertyTableModel(this))
    , m_view(new PropertyTableView(this))
{
    setupUi();
}

void PropertyGridWidget::setBridgeClient(BridgeClient *bridge) {
    m_view->setBridgeClient(bridge);
    
    // Connect property requests to bridge
    connect(m_view, &PropertyTableView::nodePropertiesRequested,
            [bridge](const QString &nodeId) {
                if (bridge) {
                    const QString requestId = QString("prop_req_%1").arg(QDateTime::currentMSecsSinceEpoch());
                    bridge->requestProperties(nodeId, requestId);
                }
            });
}

void PropertyGridWidget::showNodeProperties(const QString &nodeId) {
    m_view->setCurrentNodeId(nodeId);
    m_view->refreshProperties();
}

void PropertyGridWidget::clearProperties() {
    m_model->clear();
    m_view->setCurrentNodeId(QString());
}

void PropertyGridWidget::setupUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    
    // Header label
    auto *headerLabel = new QLabel("Properties", this);
    headerLabel->setStyleSheet("font-weight: bold; padding: 4px;");
    layout->addWidget(headerLabel);
    
    // Table view
    m_view->setModel(m_model);
    layout->addWidget(m_view);
}

} // namespace qt_spy
