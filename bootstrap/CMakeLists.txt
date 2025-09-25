add_library(qt_spy_probe_bootstrap SHARED
    src/bootstrap.cpp
    include/qt_spy/bootstrap.h
)

target_include_directories(qt_spy_probe_bootstrap
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(qt_spy_probe_bootstrap
    PRIVATE
        qt_spy_probe
        Qt5::Core
        Qt5::Network
        Qt5::Widgets
)

# Ensure the constructor is exported properly on all platforms
if (NOT WIN32)
    target_compile_definitions(qt_spy_probe_bootstrap PRIVATE QT_SPY_BOOTSTRAP_HAS_CONSTRUCTOR)
endif()
