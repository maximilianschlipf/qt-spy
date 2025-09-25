set(TST_SOURCES
    tst_cli_injection.cpp
)

add_executable(tst_cli_injection ${TST_SOURCES})

target_link_libraries(tst_cli_injection
    PRIVATE
        Qt5::Core
        Qt5::Test
)

target_compile_definitions(tst_cli_injection
    PRIVATE
        QT_SPY_CLI_BINARY_PATH="$<TARGET_FILE:qt_spy_cli>"
        QT_SPY_SAMPLE_PLAIN_MMI_PATH="$<TARGET_FILE:sample_plain_mmi>"
)

add_test(NAME cli_injection_test COMMAND tst_cli_injection)
set_tests_properties(cli_injection_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_cli_reconnect
    tst_cli_reconnect.cpp
)

target_link_libraries(tst_cli_reconnect
    PRIVATE
        qt_spy_probe
        Qt5::Core
        Qt5::Network
        Qt5::Test
        Qt5::Widgets
)

target_compile_definitions(tst_cli_reconnect
    PRIVATE
        QT_SPY_CLI_BINARY_PATH="$<TARGET_FILE:qt_spy_cli>"
)

add_test(NAME cli_reconnect_test COMMAND tst_cli_reconnect)
set_tests_properties(cli_reconnect_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
