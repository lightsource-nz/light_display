#   SSD1322 greyscale OLED driver, in the same shape as every other light_display_* module:
# an INTERFACE library a consuming project adds with add_subdirectory().
#
#   links light_display and light_core and NOTHING ELSE -- no board module, no chip port. The
# transport arrives as an io_context the caller built, so the driver never names a pin, an SPI
# instance or a platform, which is what makes it reusable on any host with the panel wired up.

add_library(light_display_ssd1322 INTERFACE)

target_include_directories(
        light_display_ssd1322 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_display_ssd1322 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/display_ssd1322.c
)

target_link_libraries(
        light_display_ssd1322 INTERFACE
        light_display
        light_core
)
