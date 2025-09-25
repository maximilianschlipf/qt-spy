#include "qt_spy/probe.h"

#include <QtTest>

#include <QProcess>
#include <QProcessEnvironment>
#include <QUuid>

namespace {

QString uniqueServerName()
{
    return QStringLiteral("qt_spy_reconnect_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
}

} // namespace

class CliReconnectTest : public QObject {
    Q_OBJECT

private slots:
    void testReconnectAfterHelperRestart();
};

void CliReconnectTest::testReconnectAfterHelperRestart()
{
#ifndef QT_SPY_CLI_BINARY_PATH
    QSKIP("CLI binary path not available at compile time.");
#endif

    qt_spy::ProbeOptions options;
    options.autoStart = false;
    options.serverName = uniqueServerName();
    qt_spy::Probe probe(options);

    const QString serverName = probe.serverName();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));

    QProcess cli;
    cli.setProgram(QStringLiteral(QT_SPY_CLI_BINARY_PATH));
    cli.setArguments({QStringLiteral("--server"), serverName,
                     QStringLiteral("--retries"), QStringLiteral("3"),
                     QStringLiteral("--no-inject")});
    cli.setProcessEnvironment(env);

    QString stdoutOutput;
    QString stderrOutput;
    QObject::connect(&cli, &QProcess::readyReadStandardOutput, [&]() {
        stdoutOutput.append(QString::fromUtf8(cli.readAllStandardOutput()));
    });
    QObject::connect(&cli, &QProcess::readyReadStandardError, [&]() {
        stderrOutput.append(QString::fromUtf8(cli.readAllStandardError()));
    });

    cli.start();
    QVERIFY2(cli.waitForStarted(5000), "Failed to start qt_spy_cli process");

    probe.start();
    if (!probe.isListening()) {
        cli.kill();
        cli.waitForFinished(3000);
        QSKIP("Probe failed to listen on local socket");
    }

    QTRY_VERIFY_WITH_TIMEOUT(stderrOutput.contains(QStringLiteral("handshake complete")), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(stdoutOutput.contains(QStringLiteral("--- snapshot ---")), 5000);

    probe.stop();

    QTRY_VERIFY_WITH_TIMEOUT(stderrOutput.contains(QStringLiteral("disconnected from server.")),
                             5000);
    QTRY_VERIFY_WITH_TIMEOUT(stderrOutput.contains(QStringLiteral("retrying in")), 5000);

    probe.start();
    if (!probe.isListening()) {
        cli.kill();
        cli.waitForFinished(3000);
        QSKIP("Probe failed to restart on local socket");
    }

    QTRY_VERIFY_WITH_TIMEOUT(
        stderrOutput.count(QStringLiteral("handshake complete")) >= 2, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(stdoutOutput.count(QStringLiteral("--- snapshot ---")) >= 2, 5000);

    cli.terminate();
    if (!cli.waitForFinished(5000)) {
        cli.kill();
        cli.waitForFinished(3000);
    }
    stdoutOutput.append(QString::fromUtf8(cli.readAllStandardOutput()));
    stderrOutput.append(QString::fromUtf8(cli.readAllStandardError()));

    probe.stop();
}

QTEST_MAIN(CliReconnectTest)
#include "tst_cli_reconnect.moc"
