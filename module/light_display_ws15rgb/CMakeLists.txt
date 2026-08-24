#   The Waveshare 1.5inch RGB OLED Module: the panel product that sits in front of an
# SSD1351, in the same shape as every other light_display_* module -- an INTERFACE library
# a consuming project adds with add_subdirectory().
#
#   it links the controller driver and nothing host-specific, deliberately. This module and
# the driver together are everything that is true about the panel wherever it is plugged
# in; the pins live with the rig that owns them.

add_library(light_display_ws15rgb INTERFACE)

target_include_directories(
        light_display_ws15rgb INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_display_ws15rgb INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/display_ws15rgb.c
)

target_link_libraries(
        light_display_ws15rgb INTERFACE
        light_display
        light_display_ssd1351
        light_core
)
