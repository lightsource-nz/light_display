
add_library(light_display_sh1107 INTERFACE)

target_include_directories(
        light_display_sh1107 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_display_sh1107 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/display_sh1107.c
)

target_link_libraries(
        light_display_sh1107 INTERFACE
        light_display
)
