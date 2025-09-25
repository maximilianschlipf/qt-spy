add_library(qt_spy_probe STATIC
    src/probe.cpp
    include/qt_spy/probe.h
)

target_include_directories(qt_spy_probe
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(qt_spy_probe
    PUBLIC
        Qt5::Core
        Qt5::Widgets
        Qt5::Network
)
