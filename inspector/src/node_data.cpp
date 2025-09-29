#include "node_data.h"
#include "qt_spy/protocol.h"

#include <QJsonValue>
#include <QRect>
#include <QSize>
#include <QPoint>

namespace qt_spy {

NodeData NodeData::fromJson(const QJsonObject &json) {
    NodeData node;
    node.id = json.value(QLatin1String(protocol::keys::kId)).toString();
    node.parentId = json.value(QLatin1String(protocol::keys::kParentId)).toString();
    
    // Extract className and objectName from properties or node object
    const QJsonObject nodeObj = json.value(QLatin1String(protocol::keys::kNode)).toObject();
    if (!nodeObj.isEmpty()) {
        node.className = nodeObj.value("className").toString();
        node.objectName = nodeObj.value("objectName").toString();
    }
    
    // Store full properties
    node.properties = json.value(QLatin1String(protocol::keys::kProperties)).toObject();
    if (node.properties.isEmpty() && !nodeObj.isEmpty()) {
        node.properties = nodeObj;
    }
    
    // Extract className and objectName from properties if not already set
    if (node.className.isEmpty()) {
        node.className = node.properties.value("className").toString();
    }
    if (node.objectName.isEmpty()) {
        node.objectName = node.properties.value("objectName").toString();
    }
    
    // Extract child IDs
    const QJsonArray childArray = json.value(QLatin1String(protocol::keys::kChildIds)).toArray();
    node.childIds.reserve(childArray.size());
    for (const QJsonValue &childValue : childArray) {
        node.childIds.append(childValue.toString());
    }
    node.childrenLoaded = !node.childIds.isEmpty() || json.contains(QLatin1String(protocol::keys::kChildIds));
    
    return node;
}

QString PropertyInfo::formatValue(const QVariant &value, const QString &type) {
    Q_UNUSED(type)
    
    switch (value.type()) {
    case QVariant::String:
        return QString("\"%1\"").arg(value.toString());
    case QVariant::Bool:
        return value.toBool() ? "true" : "false";
    case QVariant::Int:
    case QVariant::UInt:
    case QVariant::LongLong:
    case QVariant::ULongLong:
        return QString::number(value.toLongLong());
    case QVariant::Double:
        return QString::number(value.toDouble(), 'g', 6);
    case QVariant::Rect: {
        const QRect rect = value.toRect();
        return QString("%1,%2 %3x%4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
    }
    case QVariant::Size: {
        const QSize size = value.toSize();
        return QString("%1x%2").arg(size.width()).arg(size.height());
    }
    case QVariant::Point: {
        const QPoint point = value.toPoint();
        return QString("%1,%2").arg(point.x()).arg(point.y());
    }
    default:
        return value.toString();
    }
}

} // namespace qt_spy
