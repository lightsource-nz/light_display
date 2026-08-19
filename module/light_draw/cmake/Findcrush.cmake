# Finds (or builds) the crush (font-crusher) executable
#
# font-crusher is a host-side tool, so it is always built for the machine
# running the build rather than whatever target light_draw itself is being built
# for. This mirrors the picotool/pioasm pattern used by the Pico SDK: the
# tool is built as an out-of-band ExternalProject (a CMake "utility" target)
# using the host's default compiler, then imported as an executable target.
#
# If FONT_CRUSHER_PATH does not point at an existing checkout, the source is
# fetched from git instead (again following the picotool pattern), into
# FONT_CRUSHER_FETCH_FROM_GIT_PATH (or FETCHCONTENT_BASE_DIR by default).
#
# This will define the following imported targets
#
#     crush
#
cmake_minimum_required(VERSION 3.17)

if (NOT TARGET crush)
    include(ExternalProject)

    if (NOT FONT_CRUSHER_PATH OR NOT EXISTS ${FONT_CRUSHER_PATH}/CMakeLists.txt)
        if (DEFINED ENV{FONT_CRUSHER_FETCH_FROM_GIT_PATH} AND (NOT FONT_CRUSHER_FETCH_FROM_GIT_PATH))
            set(FONT_CRUSHER_FETCH_FROM_GIT_PATH $ENV{FONT_CRUSHER_FETCH_FROM_GIT_PATH})
            message("Using FONT_CRUSHER_FETCH_FROM_GIT_PATH from environment ('${FONT_CRUSHER_FETCH_FROM_GIT_PATH}')")
        endif()

        include(FetchContent)
        if (FONT_CRUSHER_FETCH_FROM_GIT_PATH)
            get_filename_component(font_crusher_INSTALL_DIR "${FONT_CRUSHER_FETCH_FROM_GIT_PATH}" ABSOLUTE)
        else()
            get_filename_component(font_crusher_INSTALL_DIR "${FETCHCONTENT_BASE_DIR}" ABSOLUTE)
        endif()

        if (NOT FONT_CRUSHER_GIT_REPOSITORY_URL)
            set(FONT_CRUSHER_GIT_REPOSITORY_URL https://github.com/lightsource-nz/font-crusher.git)
        endif()
        if (NOT FONT_CRUSHER_GIT_TAG)
            set(FONT_CRUSHER_GIT_TAG main)
        endif()

        message("No local font-crusher checkout found at '${FONT_CRUSHER_PATH}' - downloading from ${FONT_CRUSHER_GIT_REPOSITORY_URL}")
        FetchContent_Populate(font_crusher QUIET
            GIT_REPOSITORY ${FONT_CRUSHER_GIT_REPOSITORY_URL}
            GIT_TAG ${FONT_CRUSHER_GIT_TAG}
            GIT_SUBMODULES_RECURSE ON

            SOURCE_DIR ${font_crusher_INSTALL_DIR}/font-crusher-src
            BINARY_DIR ${font_crusher_INSTALL_DIR}/font-crusher-build
            SUBBUILD_DIR ${font_crusher_INSTALL_DIR}/font-crusher-subbuild
        )
        # written to the cache (not just this scope) so callers can still locate
        # font-crusher's own cmake/ support files (e.g. CrushTools.cmake) afterwards,
        # even when this runs inside a function such as light_draw_init_font_crusher()
        set(FONT_CRUSHER_PATH ${font_crusher_SOURCE_DIR} CACHE PATH "path to the root directory of the font-crusher project" FORCE)
    endif()

    set(crush_BINARY_DIR ${CMAKE_BINARY_DIR}/_deps/font-crusher)
    set(crushBuild_TARGET crushBuild)
    set(crush_TARGET crush)

    if (CMAKE_HOST_WIN32)
        set(crush_EXECUTABLE ${crush_BINARY_DIR}/bin/crush.exe)
    else()
        set(crush_EXECUTABLE ${crush_BINARY_DIR}/bin/crush)
    endif()

    if (NOT TARGET ${crushBuild_TARGET})
        ExternalProject_Add(${crushBuild_TARGET}
                PREFIX font-crusher
                SOURCE_DIR ${FONT_CRUSHER_PATH}
                BINARY_DIR ${crush_BINARY_DIR}
                CMAKE_ARGS
                    "--no-warn-unused-cli"
                    "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}"
                    "-DLIGHT_PATH:PATH=${LIGHT_PATH}"
                    "-DCMAKE_BUILD_TYPE=Debug"
                    "-DCMAKE_C_STANDARD=11"
                    "-DCMAKE_C_STANDARD_REQUIRED=ON"
                    "-DLIGHT_PLATFORM=HOST"
                    "-DLIGHT_SYSTEM=HOST_OS"
                    "-DLIGHT_RUN_MODE=DEBUG"
                    "-DCMAKE_RULE_MESSAGES=OFF" # quieten the build
                    "-DCMAKE_INSTALL_MESSAGE=NEVER" # quieten the install
                # font-crusher is only ever needed as a host tool, so only build
                # the `crush` executable itself, not its tests/demos
                BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target crush
                # tells Ninja this step actually produces crush_EXECUTABLE, so a fresh
                # (cold) build can depend on the file directly, not just order-only
                BUILD_BYPRODUCTS ${crush_EXECUTABLE}
                INSTALL_COMMAND "" # nothing to install, we run straight out of the build tree
                BUILD_ALWAYS 1 # force dependency checking
                EXCLUDE_FROM_ALL TRUE
                )
    endif()

    add_executable(${crush_TARGET} IMPORTED GLOBAL)
    set_property(TARGET ${crush_TARGET} PROPERTY IMPORTED_LOCATION
            ${crush_EXECUTABLE})

    add_dependencies(${crush_TARGET} ${crushBuild_TARGET})
endif()
