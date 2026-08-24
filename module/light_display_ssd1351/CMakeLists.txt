#   SSD1351 RGB OLED driver, in the same shape as every other light_display_* module:
# an INTERFACE library that a consuming project adds with add_subdirectory().
#
#   it links light_display and light_core and NOTHING ELSE -- no board module, no chip port.
# That is the property that makes it reusable for any host with the panel wired up: the
# transport arrives as an io_context the caller built, so the driver never names a pin, a
# SPI instance or a platform.

add_library(light_display_ssd1351 INTERFACE)

target_include_directories(
        light_display_ssd1351 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_display_ssd1351 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/display_ssd1351.c
)

target_link_libraries(
        light_display_ssd1351 INTERFACE
        light_display
        light_core
)
