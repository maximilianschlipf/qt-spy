cmake_minimum_required(VERSION 3.16)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Gui Network)

set(INSPECTOR_SOURCES
    src/main.cpp
    src/main_window.cpp
    src/connection_manager.cpp
    src/hierarchy_tree.cpp
    src/property_grid.cpp
    src/process_selector.cpp
    src/node_data.cpp
)

set(INSPECTOR_HEADERS
    src/main_window.h
    src/connection_manager.h
    src/hierarchy_tree.h
    src/property_grid.h
    src/process_selector.h
    src/node_data.h
)

add_executable(qt_spy_inspector
    ${INSPECTOR_SOURCES}
    ${INSPECTOR_HEADERS}
)

target_link_libraries(qt_spy_inspector
    Qt5::Core
    Qt5::Widgets
    Qt5::Gui
    Qt5::Network
    qt_spy_bridge
    qt_spy_probe
)

target_include_directories(qt_spy_inspector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/bridge/include
    ${CMAKE_SOURCE_DIR}/probe/include
)

# Set application properties
set_target_properties(qt_spy_inspector PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)


