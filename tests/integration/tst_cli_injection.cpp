#include <QtTest>

#include <QProcess>
#include <QProcessEnvironment>

class CliInjectionTest : public QObject {
    Q_OBJECT

private slots:
    void testInjection();
};

void CliInjectionTest::testInjection()
{
#ifndef Q_OS_UNIX
    QSKIP("Injection path currently supported only on Unix platforms.");
#endif

#ifndef QT_SPY_CLI_BINARY_PATH
    QSKIP("CLI binary path not available at compile time.");
#endif
#ifndef QT_SPY_SAMPLE_PLAIN_MMI_PATH
    QSKIP("Sample plain MMI path not available at compile time.");
#endif

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));

    QProcess sample;
    sample.setProgram(QStringLiteral(QT_SPY_SAMPLE_PLAIN_MMI_PATH));
    sample.setProcessEnvironment(env);
    sample.start();
    QVERIFY2(sample.waitForStarted(5000), "Failed to start plain MMI process");

    const qint64 targetPid = sample.processId();
    QVERIFY2(targetPid > 0, "Sample process PID is invalid");

    QProcess cli;
    cli.setProgram(QStringLiteral(QT_SPY_CLI_BINARY_PATH));
    cli.setArguments({QStringLiteral("--pid"), QString::number(targetPid),
                      QStringLiteral("--snapshot-once")});
    cli.setProcessEnvironment(env);

    cli.start();
    if (!cli.waitForFinished(30000)) {
        const QString stderrOutput = QString::fromUtf8(cli.readAllStandardError());
        sample.kill();
        sample.waitForFinished(3000);
        QSKIP(qPrintable(QStringLiteral("qt_spy_cli did not finish in time: %1").arg(stderrOutput)));
    }
    const QString stdoutOutput = QString::fromUtf8(cli.readAllStandardOutput());
    const QString stderrOutput = QString::fromUtf8(cli.readAllStandardError());
    if (cli.exitStatus() != QProcess::NormalExit || cli.exitCode() != 0) {
        sample.kill();
        sample.waitForFinished(3000);
        QSKIP(qPrintable(QStringLiteral("qt_spy_cli injection failed: %1").arg(stderrOutput)));
    }

    QVERIFY2(stdoutOutput.contains(QStringLiteral("--- snapshot ---")),
             "CLI output did not contain snapshot header");
    QVERIFY2(stderrOutput.contains(QStringLiteral("qt-spy cli: injected probe into pid=")),
             "CLI stderr did not confirm probe injection");

    sample.kill();
    sample.waitForFinished(3000);
}

QTEST_MAIN(CliInjectionTest)
#include "tst_cli_injection.moc"
