cmake_minimum_required(VERSION 3.16)
project(qt-spy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

enable_testing()

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Gui Network Test)

add_subdirectory(probe)
add_subdirectory(bridge)
add_subdirectory(bootstrap)
add_subdirectory(cli)
add_subdirectory(sample_mmi)
add_subdirectory(sample_plain_mmi)
add_subdirectory(tests)
