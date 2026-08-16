# light-display

Display support for the light framework: the display core, the panel drivers built on it, and
backlight control.

| module | what it is |
|---|---|
| `light_display` | the display core -- device registry, chunked/region flushing, render-context binding. The only compiled library here; everything else is an INTERFACE library. |
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

Each module's history is preserved -- they were imported with `git subtree`, so `git log` and
`git blame` still work through the move.

## Using it

Consuming projects resolve this repository by path rather than vendoring it as a submodule:

```cmake
light_resolve_project(LIGHT_DISPLAY light-display)
add_subdirectory(${LIGHT_DISPLAY_PATH} light_display_group)
```

`light_resolve_project()` comes from the framework (`cmake/util/light_resolve.cmake`) and looks
in this order: an existing `LIGHT_DISPLAY_PATH`, then `$ENV{LIGHT_DISPLAY_PATH}`, then a sibling
checkout at `../light-display`. So the default layout is simply this repository sitting beside
the project that uses it.

Adding the group defines **every** module, and that costs a consumer nothing: all of them except
`light_display` are INTERFACE libraries, which contribute no sources until something links them.
Link only what you need:

```cmake
target_link_libraries(my_app PRIVATE light_display light_display_st7789 light_backlight)
```

## Dependencies

`light_display` links `rend` (the framework's render layer), `light_ioport` (transport
abstraction, part of the framework) and `light_core`. The drivers link `light_display`.

A consuming project has all of these already. Building this repository standalone needs the
framework and `rend` resolvable -- see the top of `CMakeLists.txt`.
