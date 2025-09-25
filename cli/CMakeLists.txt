add_executable(qt_spy_cli
    src/main.cpp
)

target_link_libraries(qt_spy_cli
    PRIVATE
        qt_spy_bridge
        qt_spy_probe
        Qt5::Core
        Qt5::Network
)

target_compile_definitions(qt_spy_cli
    PRIVATE
        QT_SPY_BOOTSTRAP_LIBRARY_PATH="$<TARGET_FILE:qt_spy_probe_bootstrap>"
)

add_dependencies(qt_spy_cli qt_spy_probe_bootstrap)
