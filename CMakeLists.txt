
add_library(light_backlight INTERFACE)

target_include_directories(
        light_backlight INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_backlight INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/backlight.c
        ${CMAKE_CURRENT_LIST_DIR}/src/backlight_pwm.c
)

# no light_ioport here, for the same reason light_button doesn't need it: a backlight is
# driven as a PWM output on a pin, not over a transport bus, so there is no io_context
target_link_libraries(
        light_backlight INTERFACE
        light_core
)

if(LIGHT_SYSTEM STREQUAL PICO_SDK AND LIGHT_PLATFORM STREQUAL TARGET)
        target_link_libraries(
                light_backlight INTERFACE
                hardware_gpio
                hardware_pwm
        )
endif()
