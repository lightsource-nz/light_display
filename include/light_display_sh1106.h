#ifndef _LIGHT_DISPLAY_SH1106_H
#define _LIGHT_DISPLAY_SH1106_H

#include <light_display.h>

#include <stdint.h>

// unlike SH1107 (which also supports a vertical auto-increment mode), SH1106 only implements
// page addressing: the column address auto-increments on each data byte written, wrapping back
// to the start of the SAME page rather than advancing to the next one. there is no equivalent of
// SH1107_CMD_SET_ADDRMODE on real SH1106 hardware -- sending 0x20 to it is not a valid command

// SH1106's GDDRAM is 132 columns wide, but the visible glass on every common SH1106 module is
// 128px, centred in that RAM -- column 0 of the logical/canvas image lands on physical column
// SH1106_COLUMN_OFFSET_DEFAULT, not physical column 0. this is a driver-level, mounting-specific
// preference (see light_display_sh1106_set_column_offset()), not chip register state
#define SH1106_COLUMN_OFFSET_DEFAULT     2

// order update_screen()/clear_screen() sweep hardware pages in. FORWARD is the chip-native
// ascending order (page 0 .. n_pages-1); REVERSE goes the other way. page (not column) is the
// outer/bursted dimension for SH1106, since it's the column address that auto-increments within
// a page -- the opposite of SH1107, where the page address auto-increments within a column.
// defaults to FORWARD; callers that know how their rend_context's rotation maps onto physical
// pages can pick whichever sweep order actually reads as "top to bottom" for their mounting
#define SH1106_SWEEP_FORWARD            0
#define SH1106_SWEEP_REVERSE            1

// COM output scan direction (see light_display_sh1106_command_set_scan_dir()): unlike the
// simple enable-bit commands below, this toggles bit 3 of the command byte (0xC0 vs 0xC8), not
// bit 0 -- confirmed against the reference driver's own application example, which sends 0xC8
// (not 0xC1) for the remapped direction
#define SH1106_SCAN_DIR_NORMAL           0x00
#define SH1106_SCAN_DIR_REMAPPED         0x08

// display clock oscillator frequency = (fOSC * factor)
#define SH1106_FOSC_FACTOR_0_75         0x0
#define SH1106_FOSC_FACTOR_0_8          0x1
#define SH1106_FOSC_FACTOR_0_85         0x2
#define SH1106_FOSC_FACTOR_0_9          0x3
#define SH1106_FOSC_FACTOR_0_95         0x4
#define SH1106_FOSC_FACTOR_1_0          0x5     // reset value: 1.0
#define SH1106_FOSC_FACTOR_1_05         0x6
#define SH1106_FOSC_FACTOR_1_1          0x7
#define SH1106_FOSC_FACTOR_1_15         0x8
#define SH1106_FOSC_FACTOR_1_2          0x9
#define SH1106_FOSC_FACTOR_1_25         0xA
#define SH1106_FOSC_FACTOR_1_3          0xB
#define SH1106_FOSC_FACTOR_1_35         0xC
#define SH1106_FOSC_FACTOR_1_4          0xD
#define SH1106_FOSC_FACTOR_1_45         0xE
#define SH1106_FOSC_FACTOR_1_5          0xF

#define SH1106_CMD_SET_COL_ADDR_LOW     0x00
#define SH1106_CMD_SET_COL_ADDR_HIGH    0x10
#define SH1106_CMD_SET_START_LINE      0x40    // single-byte command (0x40 + line, 0-63) --
                                                // unlike SH1107's 2-byte 0xDC command
#define SH1106_CMD_SET_CONTRAST         0x81
#define SH1106_CMD_SET_SEG_REMAP        0xA0
#define SH1106_CMD_SET_FORCE_ON         0xA4
#define SH1106_CMD_SET_REVERSE          0xA6
#define SH1106_CMD_SET_MUX_RATIO        0xA8
#define SH1106_CMD_SET_DCDC             0xAD    // single mode byte (0x8A off / 0x8B on) --
                                                // unlike SH1107's bitfield-style power mode byte
#define SH1106_CMD_SET_DISPLAY_ON       0xAE
#define SH1106_CMD_SET_PAGE_ADDR        0xB0
#define SH1106_CMD_SET_VPP              0x30    // + 0..3; charge-pump output voltage level
#define SH1106_CMD_SET_SCAN_DIR         0xC0
#define SH1106_CMD_SET_DISPLAY_OFFSET   0xD3
#define SH1106_CMD_SET_DISPLAY_CLK      0xD5
#define SH1106_CMD_SET_CHARGE_PERIODS   0xD9
#define SH1106_CMD_SET_COM_PINS         0xDA    // has no equivalent in the SH1107 driver
#define SH1106_CMD_SET_VCOMH            0xDB
#define SH1106_CMD_RMW_BEGIN            0xE0
#define SH1106_CMD_NOP                  0xE3
#define SH1106_CMD_RMW_END              0xEE

extern struct display_driver *light_display_driver_sh1106();

extern struct display_device *light_display_sh1106_create_device(
        uint8_t *name, uint16_t width, uint16_t height, uint8_t bpp, struct io_context *io);
extern void light_display_sh1106_reset_device(struct display_device *dev);
extern void light_display_sh1106_chip_setup(struct display_device *dev);
extern void light_display_sh1106_clear_screen(struct display_device *dev, uint8_t value);
extern void light_display_sh1106_update_screen(struct display_device *dev);
// sets the hardware page sweep order (SH1106_SWEEP_* above) used by update_screen()/
// clear_screen(). persists across light_display_command_reset() (it's a driver-level
// preference, not chip register state), so it only needs to be called once after device
// creation
extern void light_display_sh1106_set_sweep_direction(struct display_device *dev, uint8_t direction);
// sets the physical column offset (see SH1106_COLUMN_OFFSET_DEFAULT above) added to every
// canvas x coordinate before it's written to the chip's column address register. persists
// across light_display_command_reset(), same rationale as set_sweep_direction() above
extern void light_display_sh1106_set_column_offset(struct display_device *dev, uint8_t offset);

// SH1106 LED driver commands
extern void light_display_sh1106_command_set_column_addr(struct display_device *dev, uint8_t addr);
extern void light_display_sh1106_command_set_contrast(struct display_device *dev, uint8_t level);
extern void light_display_sh1106_command_set_segment_remap(struct display_device *dev, bool enable);
extern void light_display_sh1106_command_set_multiplex_ratio(struct display_device *dev, uint8_t ratio);
extern void light_display_sh1106_command_set_force_on(struct display_device *dev, bool enable);
extern void light_display_sh1106_command_set_reverse_display(struct display_device *dev, bool enable);
extern void light_display_sh1106_command_set_display_offset(struct display_device *dev, uint8_t data);
// enable=false selects "VCC supplied externally" (DC-DC off, 0x8A); enable=true selects
// "VCC generated by internal DC-DC" (0x8B). this project's sibling SH1107 driver defaults to
// external VCC, and light_display_sh1106_chip_setup() follows the same convention
extern void light_display_sh1106_command_set_dcdc(struct display_device *dev, bool enable);
// sets the charge-pump output voltage (SH1106_CMD_SET_VPP + level, level is 0-3)
extern void light_display_sh1106_command_set_vpp(struct display_device *dev, uint8_t level);
extern void light_display_sh1106_command_set_display_on(struct display_device *dev, bool enable);
extern void light_display_sh1106_command_set_page_addr(struct display_device *dev, uint8_t addr);
// data must be one of SH1106_SCAN_DIR_NORMAL/SH1106_SCAN_DIR_REMAPPED, not a plain 0/1 index
extern void light_display_sh1106_command_set_scan_dir(struct display_device *dev, uint8_t data);
extern void light_display_sh1106_command_set_display_clock(struct display_device *dev, uint8_t div, uint8_t freq);
extern void light_display_sh1106_command_set_charge_periods(struct display_device *dev, uint8_t pre, uint8_t dis);
extern void light_display_sh1106_command_set_vcom_deselect(struct display_device *dev, uint8_t data);
// alternative=true selects alternative COM pin configuration (the common case for 128x64
// panels); remap=true swaps the left/right half of the COM pins. reset default is
// (true, false), i.e. data byte 0x12, matching the reference driver's application example
extern void light_display_sh1106_command_set_com_pins(struct display_device *dev, bool alternative, bool remap);
extern void light_display_sh1106_command_set_start_line(struct display_device *dev, uint8_t addr);
extern void light_display_sh1106_command_rmw_begin(struct display_device *dev);
extern void light_display_sh1106_command_rmw_end(struct display_device *dev);
extern void light_display_sh1106_command_no_op(struct display_device *dev);
extern void light_display_sh1106_write_data(struct display_device *dev, uint8_t data);
extern uint16_t light_display_sh1106_y_to_pages(uint16_t y);
extern uint16_t light_display_sh1106_x_to_columns(uint16_t x);
#endif
