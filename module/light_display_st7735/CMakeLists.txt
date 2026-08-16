#   ST7735/ST7735S display driver, in the same shape as every other light_display_* module:
# an INTERFACE library that a consuming project adds with add_subdirectory().

add_library(light_display_st7735 INTERFACE)

target_include_directories(
        light_display_st7735 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_display_st7735 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/display_st7735.c
)

target_link_libraries(
        light_display_st7735 INTERFACE
        light_display
        light_core
)
