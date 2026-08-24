#ifndef _LIGHT_DISPLAY_SSD1351_H
#define _LIGHT_DISPLAY_SSD1351_H

#include <light_display.h>

#include <stdint.h>

//   SSD1351: a 128x128 RGB OLED controller, 262k colours, driven here in its 65k RGB565
// mode so its RAM layout matches light_draw's 16bpp buffer byte for byte.
//
//   NOTHING IN THIS FILE KNOWS WHAT BOARD OR CHIP IT IS ON. Everything the driver touches
// goes through the io_context it is handed, which is what makes it reusable on any host
// with the panel wired up -- the pins, the SPI port and the clock are the caller's
// business. The panel PRODUCT (geometry, physical size, how its glass is mapped) is not
// here either; see light_display_ws15rgb for the Waveshare 1.5inch RGB OLED Module.
//
//   WHAT IS EASY ABOUT THIS PART, coming from ST7735: the GDDRAM is exactly 128x128, the
// same as the glass, so there is no window offset to get wrong. What is easy to get wrong
// instead is the remap byte below, which carries the colour depth AND the scan direction
// AND the RGB/BGR order in one register -- so a single wrong bit presents as "the image is
// upside down" or "red and blue are swapped" or "every other row is blank", and all three
// are the same one-line edit.

#define SSD1351_CMD_SETCOLUMN           0x15
#define SSD1351_CMD_SETROW              0x75
#define SSD1351_CMD_WRITERAM            0x5C
#define SSD1351_CMD_READRAM             0x5D
#define SSD1351_CMD_SETREMAP            0xA0
#define SSD1351_CMD_STARTLINE           0xA1
#define SSD1351_CMD_DISPLAYOFFSET       0xA2
#define SSD1351_CMD_DISPLAYALLOFF       0xA4
#define SSD1351_CMD_DISPLAYALLON        0xA5
#define SSD1351_CMD_NORMALDISPLAY       0xA6
#define SSD1351_CMD_INVERTDISPLAY       0xA7
#define SSD1351_CMD_FUNCTIONSELECT      0xAB
#define SSD1351_CMD_DISPLAYOFF          0xAE
#define SSD1351_CMD_DISPLAYON           0xAF
#define SSD1351_CMD_PRECHARGE           0xB1
#define SSD1351_CMD_DISPLAYENHANCE      0xB2
#define SSD1351_CMD_CLOCKDIV            0xB3
#define SSD1351_CMD_SETVSL              0xB4
#define SSD1351_CMD_SETGPIO             0xB5
#define SSD1351_CMD_PRECHARGE2          0xB6
#define SSD1351_CMD_SETGRAY             0xB8
#define SSD1351_CMD_USELUT              0xB9
#define SSD1351_CMD_PRECHARGELEVEL      0xBB
#define SSD1351_CMD_VCOMH               0xBE
#define SSD1351_CMD_CONTRASTABC         0xC1
#define SSD1351_CMD_CONTRASTMASTER      0xC7
#define SSD1351_CMD_MUXRATIO            0xCA
#define SSD1351_CMD_COMMANDLOCK         0xFD

//   COMMAND LOCK. Out of reset this part refuses a specific set of commands -- A2, B1, B3,
// BB, BE and C1 -- and answers them with silence rather than an error. Half of the init
// sequence is in that set, so an init that skips the unlock still produces a panel that
// accepts DISPLAYON, lights up, and shows a badly biased or blank image with nothing to
// say which steps were ignored. BOTH bytes are needed: 0x12 unlocks the ordinary commands,
// 0xB1 unlocks the ones listed above.
#define SSD1351_UNLOCK_COMMANDS         0x12
#define SSD1351_UNLOCK_EXTENDED         0xB1

//   REMAP / COLOUR DEPTH (command A0h), the one register worth reading bit by bit:
//      bit 0     address increment      0 = horizontal, 1 = vertical
//      bit 1     column address remap   0 = column 0 -> SEG0, 1 = reversed
//      bit 2     colour sequence        0 = A->B->C (RGB), 1 = C->B->A (BGR)
//      bit 3     reserved, keep 0
//      bit 4     COM scan direction     0 = COM0 -> COM[N-1], 1 = reversed
//      bit 5     COM split odd/even     0 = disable, 1 = enable
//      bits 7:6  colour depth           00 = 256, 01 = 65k, 10/11 = 262k
//
//   HORIZONTAL INCREMENT IS NOT OPTIONAL for this driver. The update path streams a whole
// window as one contiguous burst and relies on the controller wrapping from the window's
// last column back to its first and stepping the row -- exactly as ST77xx RAMWR does.
// Setting the vertical-increment bit would transpose every frame, so it is offered here
// only to be recognised, never to be set.
#define SSD1351_REMAP_VERTICAL_INC      0x01
#define SSD1351_REMAP_COLUMN_REVERSE    0x02
#define SSD1351_REMAP_BGR               0x04
#define SSD1351_REMAP_COM_REVERSE       0x10
#define SSD1351_REMAP_COM_SPLIT         0x20
#define SSD1351_REMAP_DEPTH_65K         0x40
#define SSD1351_REMAP_DEPTH_262K        0x80

//   the driver default, and the value the Waveshare and Adafruit panels of this family are
// both driven with: 65k colour, COM split odd/even (how a 128-row panel of this
// construction wires its COM lines), COM scan reversed, and the C->B->A colour sequence.
//
//   CONFIRMED ON HARDWARE 2026-08-23, against the Waveshare 1.5inch RGB OLED Module: the
// bring-up circle rendered upright, in the right colours, with no dropped rows. So this value
// is measured, rather than merely agreeing with the vendor sample it was taken from.
//
//   THE TABLE BELOW IS FOR THE NEXT PANEL, not this one. A different glass on the same
// controller can want any of these bits the other way round, and each fails visibly and
// distinctly -- so a wrong one is diagnosable by looking rather than by instrumenting:
//      red and blue swapped          -> clear SSD1351_REMAP_BGR
//      image vertically mirrored     -> clear SSD1351_REMAP_COM_REVERSE
//      alternate rows blank/ghosted  -> SSD1351_REMAP_COM_SPLIT is wrong for that glass
#define SSD1351_REMAP_DEFAULT           (SSD1351_REMAP_DEPTH_65K | SSD1351_REMAP_COM_SPLIT \
                                        | SSD1351_REMAP_COM_REVERSE | SSD1351_REMAP_BGR)

// the widest panel this controller drives, which is what bounds the clear-a-row scratch
// buffer in the driver. The GDDRAM is 128x128 and there is no wider variant of the part
#define SSD1351_MAX_WIDTH               128

//   AN OLED HAS NO BACKLIGHT. The controller's own brightness control is the contrast pair
// below, not a PWM pin, so a board built on this part has no light_backlight device and
// anything that wants to dim the screen goes through set_master_contrast() instead.
#define SSD1351_MASTER_CONTRAST_MAX     0x0F

extern struct display_driver *light_display_driver_ssd1351();

extern struct display_device *light_display_ssd1351_create_device(
        uint8_t *name, uint16_t width, uint16_t height, struct io_context *io);
extern void light_display_ssd1351_reset_device(struct display_device *dev);
extern void light_display_ssd1351_chip_setup(struct display_device *dev);
extern void light_display_ssd1351_clear_screen(struct display_device *dev, uint16_t color);

//   sets the remap byte chip_setup() applies, for a board whose glass is mounted
// differently from the default above. Persists across light_display_command_reset(), being
// a property of the panel rather than of chip register state -- the same treatment
// ST7735's GDDRAM offset needed.
//
//   to affect the FIRST frame it must be set before the device is created, since creating
// a device is what runs init_device(). A board module that needs a non-default value
// therefore spawns the io_context, sets this, and only then creates the device -- which is
// why the panel modules build their device in that order.
extern void light_display_ssd1351_set_remap(struct display_device *dev, uint8_t remap);
// 0..SSD1351_MASTER_CONTRAST_MAX, applied on top of the per-channel contrast below. See
// the no-backlight note above for why this exists at all
extern void light_display_ssd1351_set_master_contrast(struct display_device *dev, uint8_t level);
// per-channel drive, one byte each for the A/B/C segments (red/green/blue, subject to the
// colour-sequence bit). chip_setup() applies a default; this is for a panel whose white
// point needs correcting
extern void light_display_ssd1351_set_contrast(struct display_device *dev,
        uint8_t a, uint8_t b, uint8_t c);

extern void light_display_ssd1351_command_set_display_on(struct display_device *dev, bool enable);
extern void light_display_ssd1351_command_set_inversion(struct display_device *dev, bool enable);
extern void light_display_ssd1351_command_set_window(struct display_device *dev,
        uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
extern void light_display_ssd1351_command_ram_write(struct display_device *dev);

#endif
