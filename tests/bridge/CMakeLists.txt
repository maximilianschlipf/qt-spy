add_executable(tst_probe_bridge
    tst_probe_bridge.cpp
)

target_link_libraries(tst_probe_bridge
    PRIVATE
        qt_spy_probe
        Qt5::Core
        Qt5::Network
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME probe_bridge_test COMMAND tst_probe_bridge)
set_tests_properties(probe_bridge_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_bridge_client
    tst_bridge_client.cpp
)

target_link_libraries(tst_bridge_client
    PRIVATE
        qt_spy_bridge
        qt_spy_probe
        Qt5::Core
        Qt5::Network
        Qt5::Test
        Qt5::Widgets
)

add_test(NAME bridge_client_test COMMAND tst_bridge_client)
set_tests_properties(bridge_client_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
