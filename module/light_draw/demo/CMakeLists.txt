
add_executable(light_draw_demo)

target_sources(light_draw_demo PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/src/main.c
)

# rendered at build time from font-crusher's source truetype font, sized for the
# Pico-OLED-1.3 ("po13") display panel (see crossfire/screen-test's light_display_po13.h
# for the 64x128px / 17.2x32.3mm panel specs this is tuned against)
crush_add_font_target(light_draw_demo_font
        FONT ${FONT_CRUSHER_PATH}/test_resource/fonts/TypeLightSans.ttf
        DISPLAY_NAME po13 DISPLAY_WIDTH 64 DISPLAY_HEIGHT 128 DISPLAY_DIMENSION 17.2x32.3
        RENDER_NAME demo_po13 POINT_SIZE 14 PIXEL_SIZE 16
)

target_link_libraries(
        light_draw_demo PUBLIC
        light_draw
        light_draw_demo_font
)
if(LIGHT_SYSTEM STREQUAL PICO_SDK AND LIGHT_PLATFORM STREQUAL TARGET)
        if(NOT PICO_NO_HARDWARE)
                pico_enable_stdio_semihosting(light_draw_demo 1)
                target_link_libraries(light_draw_demo PUBLIC pico_stdlib)
        endif()
        pico_add_extra_outputs(light_draw_demo)
endif()
