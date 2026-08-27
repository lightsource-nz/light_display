cmake_minimum_required(VERSION 3.18)

#   This file is added two different ways, and it has to work both times.
#
#   NORMALLY it is pulled in by a consuming project that has already added the framework, so
# light_core and light_ioport are defined by the time we get here and there is nothing to
# resolve -- the whole file reduces to the add_subdirectory() list at the bottom.
#
#   STANDALONE (cmake -S . -B build) it is the top-level project, and it has to stand up the
# framework itself. That path exists so this repository can be configured and compiled on its
# own, which is what makes a build failure in a driver attributable to the driver rather than to
# whichever application happened to be building it.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        #   LIGHT_PATH is resolved by hand rather than with light_resolve_project(), for the
        # obvious reason: that function lives in the framework, so it cannot be used to find it.
        # Everything after light_preinit.cmake can and does use it.
        if(DEFINED ENV{LIGHT_PATH} AND (NOT LIGHT_PATH))
                set(LIGHT_PATH "$ENV{LIGHT_PATH}" CACHE PATH "path to the light framework project")
        endif()
        if(NOT LIGHT_PATH)
                set(LIGHT_PATH "${CMAKE_CURRENT_LIST_DIR}/../light_framework_mk3" CACHE PATH "path to the light framework project")
        endif()
        get_filename_component(LIGHT_PATH "${LIGHT_PATH}" REALPATH BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
        if(NOT EXISTS "${LIGHT_PATH}/light_init.cmake")
                message(FATAL_ERROR
                        "could not find the light framework at '${LIGHT_PATH}'.\n"
                        "Check it out beside this project as ../light_framework_mk3, or set LIGHT_PATH.")
        endif()

        include(${LIGHT_PATH}/external/light_preinit.cmake)

        #   the project and its display-core module share this name. Harmless here -- project()
        # and add_library() are different namespaces, and the group declares no target of its own
        # -- but it is why the consuming side passes a MARKER to light_resolve_project()
        project(light_display C CXX ASM)

        add_subdirectory(${LIGHT_PATH} light_framework)

        #   font-crusher, for module/light_draw's demo (see its own CMakeLists.txt). light_draw
        # declares FONT_CRUSHER_PATH itself but defaults it relative to ITSELF, which lands two
        # directories too deep under module/light_draw and misses any real sibling checkout;
        # Findcrush.cmake then falls back to fetching font-crusher from GitHub and building it,
        # which is what a standalone configure here failed on before this line existed.
        # OPTIONAL, because that fetch is a legitimate fallback for someone who has no local
        # checkout -- this only makes a local one win.
        light_resolve_project(FONT_CRUSHER font-crusher OPTIONAL)
endif()

#   EVERY module, unconditionally, and that is deliberate rather than lazy. All of these except
# light_display are INTERFACE libraries: they contribute no sources and compile nothing until a
# target links them, so a consumer that wants one driver pays nothing for the other five. It is
# the same argument the framework makes for its chip ports, and it is what lets a consumer stop
# maintaining a list of which drivers it happens to use.
#
#   light_display itself is a real library and does compile here. Both consuming projects
# already built it unconditionally, so that is not a new cost.
#
#   sh1107 before po13: po13 is the Waveshare Pico-OLED-1.3" expansion board, which is an SH1107
# panel plus a fixed geometry, and it links the sh1107 driver. CMake resolves link targets at
# generate time and would accept either order, but declaring a dependency after its dependent
# reads as an accident to anyone editing this later.
#   testing must be enabled at the ROOT for ctest to see tests registered in subdirectories --
# module/light_draw's own guard decides whether its suite is added, but without this the root
# build tree gets no CTestTestfile at all and `ctest` reports "No tests were found". Consuming
# projects (screen-test, crossfire) already do this at their roots; this is for the standalone
# host build. include(CTest) rather than bare enable_testing() so BUILD_TESTING can switch the
# suite off. HOST only for the same reason as the module-level guard: a cross-compiled test
# binary cannot run.
if(LIGHT_PLATFORM STREQUAL HOST)
        include(CTest)
endif()

add_subdirectory(module/light_display)
add_subdirectory(module/light_draw)
add_subdirectory(module/light_backlight)
add_subdirectory(module/light_display_sh1106)
add_subdirectory(module/light_display_sh1107)
add_subdirectory(module/light_display_po13)
add_subdirectory(module/light_display_st7735)
add_subdirectory(module/light_display_st7789)
#   ssd1351 before ws15rgb, for the same reason sh1107 comes before po13: ws15rgb is the
# Waveshare 1.5inch RGB OLED Module, which is SSD1351 glass plus a fixed geometry, and it
# links the driver. CMake would accept either order, but declaring a dependency after its
# dependent reads as an accident to anyone editing this later
add_subdirectory(module/light_display_ssd1351)
#   the SSD1322, a greyscale sibling of the above. Independent of every other driver here --
# it names no board and no panel module -- so its position in this list carries none of the
# ordering significance the ssd1351/ws15rgb pair does
add_subdirectory(module/light_display_ssd1322)
add_subdirectory(module/light_display_ws15rgb)
