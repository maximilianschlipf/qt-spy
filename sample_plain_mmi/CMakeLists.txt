add_executable(sample_plain_mmi
    src/main.cpp
)

target_link_libraries(sample_plain_mmi
    PRIVATE
        Qt5::Widgets
)
