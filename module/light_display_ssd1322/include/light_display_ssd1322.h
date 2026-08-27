#ifndef _LIGHT_DISPLAY_SSD1322_H
#define _LIGHT_DISPLAY_SSD1322_H

#include <light_display.h>

//   Solomon Systech SSD1322: a 480x128 OLED controller with FOUR-BIT GREYSCALE, driving
// (typically) a 256x64 panel. Two things about it shape this driver and neither is optional.
//
//   FIRST, the pixel format. Its RAM holds two 4-bit pixels per byte, high nibble leftmost.
// light_draw supports 1bpp and 16bpp and nothing else -- every path in it branches on
// px_bits == 1 or == 16 -- so there is no 4bpp context to hand this controller directly.
// Rather than add a third depth to a module every display depends on, this driver takes a
// 1BPP context and expands each pixel to 0x0 or 0xF on its way to the wire. Mono on a
// greyscale panel, using the existing stack untouched. A real 4bpp path in light_draw would
// let this render actual greys; until then the panel is capable of more than it is being told.
//
//   SECOND, the column offset, which is the classic way to get a picture that is present but
// wrong. Each SSD1322 column address covers FOUR pixels, so its 480-pixel RAM is 120 column
// addresses wide. A 256-pixel panel occupies the middle of that: (480-256)/2 = 112 pixels of
// margin each side, 112/4 = 28 column addresses, so the visible window runs 0x1C..0x5B. Send
// pixels starting at column 0 and they land off-panel and wrap, which looks like a driver bug
// anywhere but here. The offset is computed from the width rather than written as 0x1C, so a
// panel of another width gets the right answer instead of a magic number that happens to suit
// one of them.
#define SSD1322_RAM_WIDTH                       480
#define SSD1322_PIXELS_PER_COLUMN               4
#define SSD1322_PIXELS_PER_BYTE                 2

// --- command set ---
#define SSD1322_CMD_SET_COLUMN                  0x15
#define SSD1322_CMD_WRITE_RAM                   0x5C
#define SSD1322_CMD_SET_ROW                     0x75
#define SSD1322_CMD_SET_REMAP                   0xA0
#define SSD1322_CMD_SET_START_LINE              0xA1
#define SSD1322_CMD_SET_DISPLAY_OFFSET          0xA2
//   0xA4/0xA5 drive every pixel off or on FROM THE CONTROLLER, ignoring GDDRAM entirely.
// That makes 0xA5 the single most useful bring-up command on this part: it proves the panel,
// its high rail, VSL, VCOMH and the contrast settings all work, without depending on RAM
// contents, the column offset, the row addressing or any pixel data having arrived. A panel
// that lights under 0xA5 but stays dark under 0xA6 has a data-path problem; one that stays
// dark under both has a power or init problem, and those are worth telling apart early
#define SSD1322_CMD_ENTIRE_DISPLAY_OFF          0xA4
#define SSD1322_CMD_ENTIRE_DISPLAY_ON           0xA5
#define SSD1322_CMD_DISPLAY_NORMAL              0xA6
#define SSD1322_CMD_DISPLAY_INVERSE             0xA7
#define SSD1322_CMD_FUNCTION_SELECT             0xAB
#define SSD1322_CMD_DISPLAY_OFF                 0xAE
#define SSD1322_CMD_DISPLAY_ON                  0xAF
#define SSD1322_CMD_PHASE_LENGTH                0xB1
#define SSD1322_CMD_CLOCK_DIVIDER               0xB3
#define SSD1322_CMD_DISPLAY_ENHANCE_A           0xB4
#define SSD1322_CMD_SET_GPIO                    0xB5
#define SSD1322_CMD_SECOND_PRECHARGE            0xB6
#define SSD1322_CMD_GREYSCALE_DEFAULT           0xB9
#define SSD1322_CMD_PRECHARGE_VOLTAGE           0xBB
#define SSD1322_CMD_SET_VCOMH                   0xBE
#define SSD1322_CMD_CONTRAST_CURRENT            0xC1
#define SSD1322_CMD_MASTER_CONTRAST             0xC7
#define SSD1322_CMD_SET_MUX_RATIO               0xCA
#define SSD1322_CMD_COMMAND_LOCK                0xFD

//   the command lock, and it is FIRST for the same reason the SSD1351's is: most of the
// configuration below is in the locked set and is silently ignored until this is sent
#define SSD1322_UNLOCK_COMMANDS                 0x12

//   default re-map: horizontal address increment, column address remap, and the nibble order
// that pairs with it. 0x14/0x11 is what every working driver for this part uses, and the
// second byte is the dual-COM mode that a 64-row panel needs -- a 0x00 there gives a display
// that shows every other row
#define SSD1322_REMAP_DEFAULT_LOW               0x14
#define SSD1322_REMAP_DEFAULT_HIGH              0x11

// full brightness; both are reduced by set_contrast()/set_master_contrast()
#define SSD1322_CONTRAST_DEFAULT                0x9F
#define SSD1322_MASTER_CONTRAST_DEFAULT         0x0F
#define SSD1322_MASTER_CONTRAST_MAX             0x0F

//   Display Enhancement A (0xB4), first byte -- and the value in it that is MOST LIKELY to be
// wrong on an unfamiliar module. Its low two bits select where the segment low voltage comes
// from: 0xA0 says the board supplies VSL through external circuitry, 0xA2 says use the
// controller's internal one.
//   getting this backwards gives a completely dark panel with every other setting correct,
// because the segments have no drive. A module with a few discrete transistors near the boost
// probably has the external circuit and wants 0xA0; one with nothing but the boost IC wants
// 0xA2. It is worth trying both before suspecting anything subtler -- the cost is one byte
#define SSD1322_ENHANCE_A_VSL_EXTERNAL          0xA0
#define SSD1322_ENHANCE_A_VSL_INTERNAL          0xA2
#ifndef SSD1322_ENHANCE_A_VSL
#define SSD1322_ENHANCE_A_VSL                   SSD1322_ENHANCE_A_VSL_EXTERNAL
#endif

extern struct display_driver *light_display_driver_ssd1322();
//   `io` must be a 4-wire SPI context. The panel is driven from a 1BPP light_draw context --
// see the note at the top of this header -- so the caller creates its render context with
// px_bits 1, not 4 and not 16
extern struct display_device *light_display_ssd1322_create_device(
                uint8_t *name, uint16_t width, uint16_t height, struct io_context *io);

extern void light_display_ssd1322_chip_setup(struct display_device *dev);
extern void light_display_ssd1322_reset_device(struct display_device *dev);
extern void light_display_ssd1322_clear_screen(struct display_device *dev, uint16_t value);
extern void light_display_ssd1322_command_set_display_on(struct display_device *dev, bool enable);
extern void light_display_ssd1322_command_set_inversion(struct display_device *dev, bool enable);
//   drives every pixel on or off from the controller, ignoring GDDRAM -- see
// SSD1322_CMD_ENTIRE_DISPLAY_ON for why this is the first thing to try on a dark panel.
// Passing false returns to showing RAM contents (0xA6 normal), not to an all-off screen
extern void light_display_ssd1322_command_set_entire_on(struct display_device *dev, bool enable);
//   re-sends Display Enhancement A with a different VSL selection, so a rig can try both
// without a rebuild -- see SSD1322_ENHANCE_A_VSL for why this is the value most likely wrong
// on an unfamiliar module. Takes effect immediately, with the display running
extern void light_display_ssd1322_set_vsl(struct display_device *dev, uint8_t vsl_select);
extern void light_display_ssd1322_set_contrast(struct display_device *dev, uint8_t level);
extern void light_display_ssd1322_set_master_contrast(struct display_device *dev, uint8_t level);
//   arms the RAM window for a region and leaves the controller expecting pixel data. Public
// because bring-up wants to poke a pattern at the panel without going through the async path
extern void light_display_ssd1322_command_set_window(struct display_device *dev,
                uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

#endif
