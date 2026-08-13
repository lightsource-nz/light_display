#ifndef _LIGHT_DISPLAY_ST7735_H
#define _LIGHT_DISPLAY_ST7735_H

#include <light_display.h>

#include <stdint.h>

//   THE OFFSETS ARE THE WHOLE DIFFICULTY WITH THIS CONTROLLER. ST7735's GDDRAM is 132x162,
// and a smaller panel is a WINDOW into it rather than the whole of it -- so every CASET/RASET
// must be shifted by where that window starts. Get it wrong and the image is displaced, or
// wraps around the edge, with nothing reporting an error.
//   Worse, the offset is a property of the glass rather than of the controller, so two boards
// with the same ST7735S need different values. WeAct ship the 0.96in MiniSTM32H7xx panel in
// two variants and their own driver distinguishes them:
//
//      HannStar   portrait  x+26 y+1     landscape  x+1  y+26
//      BOE        portrait  x+24 y+0     landscape  x+0  y+24
//
// and the two ALSO differ in colour order (BGR vs RGB), so a mismatch shows up as a couple of
// pixels of displacement AND red/blue swapped. WeAct's own board code selects HannStar in
// landscape-rot180, which is what the defaults below are; if the image is off by two pixels
// and the colours are inverted, the panel is the BOE variant, not a bug to hunt.
//   VERIFIED on a MiniSTM32H7xx: a one-pixel border drawn at the buffer's outermost ring sits
// flush against all four edges of the glass with these values, and RGB565 0xF800 renders red
// rather than blue -- so both the offsets and the HannStar colour order are confirmed rather
// than taken from the vendor's defaults.
#define ST7735_COL_OFFSET_DEFAULT       1
#define ST7735_ROW_OFFSET_DEFAULT       26

// 0.96in panel, 80x160 in portrait -- so 160x80 in the landscape orientation the board uses
#define ST7735_0_9_WIDTH                80
#define ST7735_0_9_HEIGHT               160

// MADCTL bits. Same layout as ST7789's, this being the same vendor's earlier part
#define ST7735_MADCTL_MY                0x80    // row address order
#define ST7735_MADCTL_MX                0x40    // column address order
#define ST7735_MADCTL_MV                0x20    // row/column exchange
#define ST7735_MADCTL_ML                0x10    // vertical refresh order
#define ST7735_MADCTL_BGR               0x08    // RGB/BGR order (0 = RGB, 1 = BGR)
#define ST7735_MADCTL_MH                0x04    // horizontal refresh order

// landscape rotated 180 degrees, which is how the panel is mounted on this board: MV exchanges
// the axes, MY flips the row order to get the 180. Combined with BGR for the HannStar variant
#define ST7735_MADCTL_LANDSCAPE_ROT180  (ST7735_MADCTL_MV | ST7735_MADCTL_MY | ST7735_MADCTL_BGR)

// COLMOD pixel format -- 16bpp/RGB565 is the only format this driver supports, matching rend's
// 16bpp buffer layout byte for byte
#define ST7735_COLMOD_16BPP             0x05

#define ST7735_CMD_NOP                  0x00
#define ST7735_CMD_SWRESET              0x01
#define ST7735_CMD_SLPIN                0x10
#define ST7735_CMD_SLPOUT               0x11
#define ST7735_CMD_NORON                0x13
#define ST7735_CMD_INVOFF               0x20
#define ST7735_CMD_INVON                0x21
#define ST7735_CMD_DISPOFF              0x28
#define ST7735_CMD_DISPON               0x29
#define ST7735_CMD_CASET                0x2A    // column address set
#define ST7735_CMD_RASET                0x2B    // row address set
#define ST7735_CMD_RAMWR                0x2C
#define ST7735_CMD_MADCTL               0x36
#define ST7735_CMD_COLMOD               0x3A
//   frame rate and power control. ST7789 can be brought up on its defaults; this part cannot
// -- without these the panel either stays dark or shows a badly biased image, because its
// defaults assume a different supply arrangement
#define ST7735_CMD_FRMCTR1              0xB1    // frame rate, normal mode
#define ST7735_CMD_FRMCTR2              0xB2    // frame rate, idle mode
#define ST7735_CMD_FRMCTR3              0xB3    // frame rate, partial mode
#define ST7735_CMD_INVCTR               0xB4    // display inversion control
#define ST7735_CMD_PWCTR1               0xC0
#define ST7735_CMD_PWCTR2               0xC1
#define ST7735_CMD_PWCTR3               0xC2
#define ST7735_CMD_PWCTR4               0xC3
#define ST7735_CMD_PWCTR5               0xC4
#define ST7735_CMD_VMCTR1               0xC5    // VCOM
#define ST7735_CMD_GMCTRP1              0xE0    // positive gamma
#define ST7735_CMD_GMCTRN1              0xE1    // negative gamma

extern struct display_driver *light_display_driver_st7735();

extern struct display_device *light_display_st7735_create_device(
        uint8_t *name, uint16_t width, uint16_t height, struct io_context *io);
extern void light_display_st7735_reset_device(struct display_device *dev);
extern void light_display_st7735_chip_setup(struct display_device *dev);
extern void light_display_st7735_clear_screen(struct display_device *dev, uint16_t color);
// sets the GDDRAM offset added to every window before CASET/RASET -- see the note at the top,
// which is where the real explanation lives. Persists across light_display_command_reset(),
// being a property of the glass rather than chip register state
extern void light_display_st7735_set_offset(struct display_device *dev, uint16_t col_offset, uint16_t row_offset);

extern void light_display_st7735_command_sw_reset(struct display_device *dev);
extern void light_display_st7735_command_sleep_out(struct display_device *dev);
extern void light_display_st7735_command_set_colmod(struct display_device *dev, uint8_t format);
extern void light_display_st7735_command_set_madctl(struct display_device *dev, uint8_t bits);
extern void light_display_st7735_command_set_inversion(struct display_device *dev, bool enable);
extern void light_display_st7735_command_set_display_on(struct display_device *dev, bool enable);
extern void light_display_st7735_command_set_window(struct display_device *dev,
        uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
extern void light_display_st7735_command_ram_write(struct display_device *dev);

#endif
