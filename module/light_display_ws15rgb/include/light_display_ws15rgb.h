#ifndef _LIGHT_DISPLAY_WS15RGB_H
#define _LIGHT_DISPLAY_WS15RGB_H

#include <light_display_ssd1351.h>

#include <stdint.h>

//   The Waveshare "1.5inch RGB OLED Module" -- 128x128 RGB OLED glass on an SSD1351, on a
// breakout with a 7-pin header (VCC, GND, DIN, CLK, CS, DC, RST).
//
//   WHAT THIS MODULE IS AND IS NOT. It is the PANEL PRODUCT: the geometry, the physical
// size, the colour depth, and how this particular glass is mapped onto the controller. It
// is NOT a board: unlike light_display_po13, which is an expansion board that plugs into a
// fixed Pico header and can therefore name its own pins, this is a flying-lead module that
// goes wherever it is wired. So it declares NO pins, NO SPI port and NO clock, and takes a
// finished io_context from whoever wired it up.
//
//   That split is what lets the same two modules serve any host: light_display_ssd1351
// knows the controller, this knows the glass in front of it, and a rig module somewhere
// else knows which wires went where. Nothing in either file names a chip or a board.

// 128x128, the whole of the SSD1351 GDDRAM -- so unlike the ST77xx panels there is no
// window offset to apply, and no variant of this product with different geometry
#define WS15RGB_WIDTH                   128
#define WS15RGB_HEIGHT                  128
// RGB565. The controller can do 262k colour, but light_draw is 16bpp, and matching the two
// exactly is what lets an update stream straight out of the render buffer
#define WS15RGB_BPP                     16

//   the active area, in millimetres, for anything that has to resolve a physical size --
// crush resolving a point size against a real display being the case that needs it.
// DERIVED, not read off a datasheet: 128x128 square pixels over a 1.5" diagonal works out
// at 26.9mm a side. Close enough for type sizing; do not use it as a mechanical dimension
#define WS15RGB_DIMENSION_MM_X          26.9
#define WS15RGB_DIMENSION_MM_Y          26.9

//   the panel is square, so it has no native orientation of its own to rotate away from --
// which is why there is no WS15RGB_ROTATION here. Whichever way up a rig mounts it is a
// rig fact, and belongs with the rig's pins

//   builds the device on an io_context the caller has already set up -- see the note above
// about why this call takes a transport rather than making one. The remap is the driver
// default (SSD1351_REMAP_DEFAULT), which is what this glass wants; a rig that mounts the
// module rotated or mirrored calls light_display_ssd1351_set_remap() rather than reaching
// for a second constructor
extern struct display_device *light_display_ws15rgb_create_device(uint8_t *name, struct io_context *io);

#endif
