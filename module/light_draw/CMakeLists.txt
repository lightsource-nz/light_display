
#   font-crusher is used only by this module's own demo, to render the font it draws with at
# build time. Defaulted relative to this module's own directory, same as the standalone `rend`
# project used to do -- wrong two levels deep under module/light_draw, but harmless: the
# top-level light_display/CMakeLists.txt resolves the real sibling checkout first (via
# light_resolve_project(FONT_CRUSHER ...)) and sets it as a CACHE var, which wins here since
# set(... CACHE ...) never overwrites an existing cache entry without FORCE
set(FONT_CRUSHER_PATH "${CMAKE_CURRENT_LIST_DIR}/../../../font-crusher" CACHE PATH "path to the root directory of the font-crusher project")
get_filename_component(FONT_CRUSHER_PATH "${FONT_CRUSHER_PATH}" REALPATH BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
option(LIGHT_DRAW_NO_FONT_CRUSHER "Disable use/requirement of the font-crusher (crush) tool" OFF)

# Check crush (font-crusher) is available, building it as a host-native
# ExternalProject "utility" target if not already found -- mirrors how the
# Pico SDK lazily builds/finds picotool via pico_init_picotool()
function(light_draw_init_font_crusher)
        if (NOT TARGET crush AND NOT DEFINED crush_FOUND)
                set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_LIST_DIR}/cmake)
                find_package(crush MODULE)
        endif()

        if (TARGET crush AND NOT DEFINED ENV{_CRUSH_FOUND_THIS_RUN})
                # Internal environment variable to prevent printing multiple times
                set(ENV{_CRUSH_FOUND_THIS_RUN} 1)

                get_property(crush_location TARGET crush PROPERTY IMPORTED_LOCATION)
                message("Using crush (font-crusher) from ${crush_location}")
        endif()

        if (TARGET crush)
                set(crush_FOUND 1 PARENT_SCOPE)
                # Findcrush.cmake may have just re-pointed FONT_CRUSHER_PATH at a freshly
                # fetched checkout (via CACHE ... FORCE) when no local one was found -- that
                # update only lands in this function's own scope, so it has to be forwarded
                # to the caller explicitly or the caller keeps seeing its original (possibly
                # nonexistent) path and silently fails to find CrushTools.cmake there
                set(FONT_CRUSHER_PATH ${FONT_CRUSHER_PATH} PARENT_SCOPE)
        else()
                message("No crush (font-crusher) found")
        endif()
endfunction()

if(NOT LIGHT_DRAW_NO_FONT_CRUSHER)
        light_draw_init_font_crusher()
        if(crush_FOUND AND EXISTS "${FONT_CRUSHER_PATH}/cmake/CrushTools.cmake")
                # provides crush_invoke()/crush_add_font_target() etc. for turning font files
                # into light_draw_font_t sources at build time
                include(${FONT_CRUSHER_PATH}/cmake/CrushTools.cmake)
        endif()
endif()

add_subdirectory(demo)

add_library(light_draw INTERFACE)

target_include_directories(
        light_draw INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_draw INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/light_draw.c
        ${CMAKE_CURRENT_LIST_DIR}/src/light_draw_api.c
        ${CMAKE_CURRENT_LIST_DIR}/src/light_draw_draw.c
)

if(DEFINED LIGHT_DRAW_DEBUG_PIXEL_TRACE)
        target_compile_definitions(
                light_draw INTERFACE
                LIGHT_DRAW_DEBUG_PIXEL_TRACE
        )
endif()

if(DEFINED LIGHT_DRAW_DEBUG_API_TRACE)
        target_compile_definitions(
                light_draw INTERFACE
                LIGHT_DRAW_DEBUG_API_TRACE
        )
endif()

target_link_libraries(
        light_draw INTERFACE
        light_core
)
