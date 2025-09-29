#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QVector>
#include <QVariant>

namespace qt_spy {

struct QtProcessInfo {
    qint64 pid = 0;
    QString name;
    QString commandLine;
    QString windowTitle;
    bool hasQtLibraries = false;
    bool hasExistingProbe = false;
    
    QString displayName() const {
        if (!windowTitle.isEmpty()) {
            return QString("%1 - \"%2\"").arg(name, windowTitle);
        }
        return name;
    }
};

struct NodeData {
    QString id;
    QString parentId;
    QString className;
    QString objectName;
    QJsonObject properties;
    QVector<QString> childIds;
    bool childrenLoaded = false;
    
    QString displayName() const {
        if (!objectName.isEmpty()) {
            return QString("%1 (%2)").arg(objectName, className);
        }
        return className;
    }
    
    static NodeData fromJson(const QJsonObject &json);
};

struct PropertyInfo {
    QString name;
    QVariant value;
    QString type;
    QString displayValue;
    
    PropertyInfo() = default;
    PropertyInfo(const QString &n, const QVariant &v, const QString &t = QString())
        : name(n), value(v), type(t), displayValue(formatValue(v, t)) {}
        
private:
    static QString formatValue(const QVariant &value, const QString &type);
};

} // namespace qt_spy
