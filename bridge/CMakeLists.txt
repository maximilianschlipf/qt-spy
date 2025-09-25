add_library(qt_spy_bridge STATIC
    src/bridge_client.cpp
    include/qt_spy/bridge_client.h
)

target_include_directories(qt_spy_bridge
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/probe/include
)

target_link_libraries(qt_spy_bridge
    PUBLIC
        Qt5::Core
        Qt5::Network
)
