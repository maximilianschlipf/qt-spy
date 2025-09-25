#pragma once

#include <QtGlobal>

namespace qt_spy {

Q_DECL_EXPORT void start_probe();

} // namespace qt_spy

extern "C" Q_DECL_EXPORT void qt_spy_start_probe();

