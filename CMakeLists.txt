
add_library(light_display)

target_include_directories(
        light_display PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_display PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/display.c
)

# light_display_ioport is an aggregate submodule,
# so light_display depends on it
target_link_libraries(
        light_display PUBLIC
        rend
        light_display_ioport
        light_framework
)

if(LIGHT_SYSTEM STREQUAL PICO_SDK)
        target_link_libraries(
                light_display PUBLIC
        )
endif()
