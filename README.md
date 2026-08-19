# light_display

Display support for the light framework: the display core, the panel drivers built on it, and
backlight control.

Note that this project and its display-core module share the name `light_display` -- the group
is this repository, the module is `module/light_display` inside it. That matters in one place
only, and it is handled: see *Using it* below.

| module | what it is |
|---|---|
| `light_display` | the display core -- device registry, chunked/region flushing, render-context binding. The only compiled library here; everything else is an INTERFACE library. |
| `light_draw` | the render layer -- image buffers, drawing primitives, fonts. Formerly the standalone `rend` project, now vendored as part of this group. |
| `light_backlight` | PWM backlight control with fades, driven through `light_platform`'s PWM API |
| `light_display_sh1106` | SH1106 monochrome OLED controller |
| `light_display_sh1107` | SH1107 monochrome OLED controller |
| `light_display_po13` | Waveshare Pico-OLED-1.3" expansion board -- panel geometry over the SH1107 driver |
| `light_display_st7735` | ST7735/ST7735S colour TFT controller |
| `light_display_st7789` | ST7789 colour TFT controller |

## Why these live together

They were seven separate git repositories, which meant seven checkouts, seven submodule
pointers per consuming project, and seven version numbers to keep in step. Nothing was gained
by it: a change to the display core's device contract lands in the drivers at the same moment,
and as separate repositories git could not express that as one commit.

Each module's history is preserved: they were imported with `git subtree`, so every original
commit is in this repository and `git blame` reaches straight through the move (blame on
`module/light_display/src/display.c` still reports commits from 2024, under their original
`src/display.c` paths).

One wrinkle worth knowing, because it looks like data loss and is not. A path-filtered
`git log -- module/light_backlight` shows only the import commit: git's history simplification
stops at a merge whose first parent already contains the result. The commits are there --

```sh
git log --oneline --full-history -- module/light_backlight   # includes the merges
git log --oneline <import-commit>^2                          # the module's own history alone
```

-- and plain `git log` with no path filter lists them all.

## Using it

Consuming projects resolve this repository by path rather than vendoring it as a submodule:

```cmake
light_resolve_project(LIGHT_DISPLAY light_display MARKER module/light_display/CMakeLists.txt)
add_subdirectory(${LIGHT_DISPLAY_PATH} light_display_group)
```

`light_resolve_project()` comes from the framework (`cmake/util/light_resolve.cmake`) and looks
in this order: an existing `LIGHT_DISPLAY_PATH`, then `$ENV{LIGHT_DISPLAY_PATH}`, then an
in-project `module/light_display`, then a sibling checkout at `../light_display`. So the default
layout is simply this repository sitting beside the project that uses it.

**Pass the `MARKER`.** It is the one place the shared name bites. `module/light_display` is both
"an in-project checkout of the group" and "a checkout of the display-core module" -- and the
resolver prefers it over the sibling. Without a marker that only the group has, a stray module
checkout resolves as if it were the whole group, `add_subdirectory` succeeds, and the build then
fails on six drivers that were never added, naming none of the cause.
`module/light_display/CMakeLists.txt` exists only in the group, so it settles the question.

Adding the group defines **every** module, and that costs a consumer nothing: all of them except
`light_display` are INTERFACE libraries, which contribute no sources until something links them.
Link only what you need:

```cmake
target_link_libraries(my_app PRIVATE light_display light_display_st7789 light_backlight)
```

## Dependencies

`light_display` links `light_draw` (the render layer, part of this group), `light_ioport`
(transport abstraction, part of the framework) and `light_core`. The drivers link
`light_display`.

A consuming project has all of these already. Building this repository standalone needs the
framework resolvable -- see the top of `CMakeLists.txt`.
