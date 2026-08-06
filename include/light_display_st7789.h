#ifndef _LIGHT_DISPLAY_ST7789_H
#define _LIGHT_DISPLAY_ST7789_H

#include <light_display.h>

#include <stdint.h>

// ST7789's native GDDRAM is up to 240x320; smaller panels (like this board's 240x280)
// are centred or offset within that RAM, chip-instance-specific and not knowable from
// the datasheet alone -- default to no offset, adjust empirically once on real hardware,
// same as SH1106's column-offset situation
#define ST7789_COL_OFFSET_DEFAULT       0
#define ST7789_ROW_OFFSET_DEFAULT       0

// MADCTL bits (see light_display_st7789_command_set_madctl())
#define ST7789_MADCTL_MY                0x80    // row address order
#define ST7789_MADCTL_MX                0x40    // column address order
#define ST7789_MADCTL_MV                0x20    // row/column exchange
#define ST7789_MADCTL_ML                0x10    // vertical refresh order
#define ST7789_MADCTL_BGR                0x08    // RGB/BGR order (0 = RGB, 1 = BGR)
#define ST7789_MADCTL_MH                0x04    // horizontal refresh order

// COLMOD pixel format (see light_display_st7789_command_set_colmod()) -- 16bpp/RGB565
// on both the MCU and RGB interfaces is the only format this driver supports
#define ST7789_COLMOD_16BPP             0x55

#define ST7789_CMD_NOP                  0x00
#define ST7789_CMD_SWRESET               0x01
#define ST7789_CMD_SLPIN                0x10
#define ST7789_CMD_SLPOUT               0x11
#define ST7789_CMD_NORON                0x13
#define ST7789_CMD_INVOFF               0x20
#define ST7789_CMD_INVON                0x21
#define ST7789_CMD_DISPOFF              0x28
#define ST7789_CMD_DISPON               0x29
#define ST7789_CMD_CASET                0x2A    // column address set
#define ST7789_CMD_RASET                0x2B    // row address set
#define ST7789_CMD_RAMWR                0x2C
#define ST7789_CMD_MADCTL               0x36
#define ST7789_CMD_COLMOD               0x3A

extern struct display_driver *light_display_driver_st7789();

extern struct display_device *light_display_st7789_create_device(
        uint8_t *name, uint16_t width, uint16_t height, struct io_context *io);
extern void light_display_st7789_reset_device(struct display_device *dev);
extern void light_display_st7789_chip_setup(struct display_device *dev);
extern void light_display_st7789_clear_screen(struct display_device *dev, uint16_t color);
extern void light_display_st7789_update_screen(struct display_device *dev);
// sets the physical GDDRAM offset (see ST7789_*_OFFSET_DEFAULT above) added to the
// column/row window before every CASET/RASET. persists across
// light_display_command_reset() -- it's a driver-level/mounting preference, not chip
// register state
extern void light_display_st7789_set_offset(struct display_device *dev, uint16_t col_offset, uint16_t row_offset);

// ST7789 commands
extern void light_display_st7789_command_sw_reset(struct display_device *dev);
extern void light_display_st7789_command_sleep_out(struct display_device *dev);
extern void light_display_st7789_command_set_colmod(struct display_device *dev, uint8_t format);
// bitwise-OR of ST7789_MADCTL_* above
extern void light_display_st7789_command_set_madctl(struct display_device *dev, uint8_t bits);
extern void light_display_st7789_command_set_inversion(struct display_device *dev, bool enable);
extern void light_display_st7789_command_set_display_on(struct display_device *dev, bool enable);
// sets the column/row address window (inclusive) that the following RAMWR burst will
// fill -- x0/x1 and y0/y1 are already offset-adjusted (see light_display_st7789_set_offset())
extern void light_display_st7789_command_set_window(struct display_device *dev,
        uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
// sends the RAMWR command byte -- caller follows with light_display_ioport_send_data_burst()
// of the actual pixel data as a separate call. CS toggles independently per
// light_display_ioport call (same as every other command+data sequence in this codebase,
// e.g. SH1106/SH1107's column-address-then-burst pattern) -- ST7789 only needs DC to
// correctly distinguish command vs. data bytes, not a continuously-held CS across both
extern void light_display_st7789_command_ram_write(struct display_device *dev);

#endif
