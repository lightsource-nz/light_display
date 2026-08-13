#   ST7735S display driver. In-tree for now, unlike the other light_display_* drivers, which
# are each their own repository and a submodule here -- it is published once it has been proven
# on hardware, rather than creating a repository for something unverified.

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
