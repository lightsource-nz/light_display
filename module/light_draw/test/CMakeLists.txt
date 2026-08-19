
# HOST ONLY. these tests run the drawing code and inspect the buffer it produced, so the
# binary has to execute on the machine doing the building -- a cross-compiled one would need
# an emulator to say anything at all. the guard lives in the parent CMakeLists so this file is
# never even reached on a target build.
add_executable(test_light_draw ${CMAKE_CURRENT_LIST_DIR}/test_light_draw.c)

# light_draw is an INTERFACE library carrying its own sources and its dependency on light_core, so
# linking it is all the test needs -- no hand-assembled include paths, which is what makes
# this work identically whether light_draw is built standalone or as a submodule of something else
target_link_libraries(test_light_draw PRIVATE light_draw)

# libm is separate on unix and part of the C runtime on mingw; asking for it unconditionally
# fails on the latter
find_library(LIGHT_DRAW_TEST_LIBM m)
if(LIGHT_DRAW_TEST_LIBM)
        target_link_libraries(test_light_draw PRIVATE ${LIGHT_DRAW_TEST_LIBM})
endif()

# one CTest case per test function rather than one for the whole binary: a failure then names
# itself in the ctest summary instead of being one line somewhere inside a wall of output.
# the binary takes a case name and runs only that one.
#
# KEEP IN SYNC with test_cases[] in test_light_draw.c. `test_light_draw --list` prints the
# names the binary actually has, and an unknown name here exits non-zero rather than passing
# quietly, so a mismatch shows up as a failing test rather than as silent under-testing
set(LIGHT_DRAW_TEST_CASES
        arc_no_gaps
        arc_accuracy
        arc_endpoints
        arc_clipping
        rect_rounded_joins
        rect_rounded_fill
        rect_rounded_corner_mask
        circle_fill
        circle_fill_matches_outline
        demo_title_clears_curve
        transform_round_trip
        transform_maps_corners
        transform_rect_orders_its_output
        untransform_clamps_outside_points
        scale_inscribed_shrinks_off_axis
        transform_round_trip_with_flips
        flip_mirrors_the_right_axis
        double_buffer_swaps
)
foreach(_case ${LIGHT_DRAW_TEST_CASES})
        add_test(NAME light_draw.${_case} COMMAND test_light_draw ${_case})
endforeach()
