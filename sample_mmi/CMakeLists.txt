add_executable(sample_mmi
    src/main.cpp
)

target_link_libraries(sample_mmi
    PRIVATE
        qt_spy_probe
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
)

