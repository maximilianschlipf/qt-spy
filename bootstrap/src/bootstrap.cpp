#include "qt_spy/bootstrap.h"

#include "qt_spy/probe.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

namespace {

enum class BootstrapState { Idle, WaitingForApp, Started };

class ProbeBootstrap {
public:
    static ProbeBootstrap &instance()
    {
        static ProbeBootstrap inst;
        return inst;
    }

    void ensure()
    {
        if (m_state == BootstrapState::Started) {
            return;
        }

        if (auto *app = QCoreApplication::instance()) {
            if (QThread::currentThread() == app->thread()) {
                startProbe(app);
            } else {
                QMetaObject::invokeMethod(app,
                                          [this]() { startProbe(QCoreApplication::instance()); },
                                          Qt::QueuedConnection);
            }
        } else {
            m_state = BootstrapState::WaitingForApp;
        }
    }

private:
    void startProbe(QObject *context)
    {
        if (m_state == BootstrapState::Started) {
            return;
        }

        qt_spy::ProbeOptions options;
        options.autoStart = true;
        // Use the core application instance as parent when available to align lifetimes.
        QObject *parent = context ? context : QCoreApplication::instance();
        m_probe = new qt_spy::Probe(options, parent);
        m_state = BootstrapState::Started;
    }

    BootstrapState m_state = BootstrapState::Idle;
    qt_spy::Probe *m_probe = nullptr;
};

} // namespace

namespace qt_spy {

void start_probe()
{
    ProbeBootstrap::instance().ensure();
}

} // namespace qt_spy

extern "C" Q_DECL_EXPORT void qt_spy_start_probe()
{
    qt_spy::start_probe();
}

static void qt_spy_start_probe_post_app()
{
    qt_spy_start_probe();
}

Q_COREAPP_STARTUP_FUNCTION(qt_spy_start_probe_post_app)

#ifdef QT_SPY_BOOTSTRAP_HAS_CONSTRUCTOR
__attribute__((constructor)) static void qt_spy_bootstrap_ctor()
{
    qt_spy_start_probe();
}
#endif

