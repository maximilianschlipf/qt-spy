#pragma once

#include <functional>
#include <memory>

#include <QObject>
#include <QLocalServer>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QIODevice;
class QLocalSocket;
QT_END_NAMESPACE

namespace qt_spy {

struct ProbeOptions {
    QString serverName;           // optional override for server name
    bool autoStart = true;        // start listening immediately when constructed
};

class Probe : public QObject {
    Q_OBJECT
public:
    explicit Probe(const ProbeOptions &options = ProbeOptions{}, QObject *parent = nullptr);

    QString serverName() const;
    bool isListening() const;

public slots:
    void start();
    void stop();

private:
    void setupServer();
    void handleNewConnection();
    void removeConnection(class ProbeConnection *connection);

    QString m_serverName;
    bool m_autoStart = true;
    std::unique_ptr<QLocalServer> m_server;
    QVector<class ProbeConnection *> m_connections;
};

QString defaultServerName();
QString defaultServerName(qint64 pid);
QString defaultServerName(const QString &applicationName, qint64 pid);

} // namespace qt_spy
