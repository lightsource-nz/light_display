#ifndef _LIGHT_DISPLAY_PO13_H
#define _LIGHT_DISPLAY_PO13_H

#include <light_display_sh1107.h>

#include <stdint.h>

// the Pico-OLED-1.3's glass is physically portrait (64 wide x 128 tall), per direct
// on-device observation -- an earlier "landscape" conclusion was based on misreading a
// sideways photo of the board and led to a lot of unnecessary chasing in the SH1107
// driver's addressing logic
#define PO13_WIDTH                      64
#define PO13_HEIGHT                     128
#define PO13_BPP                        1

#define PO13_PIN_RESET                  12
#define PO13_PIN_CS                     9
#define PO13_PIN_DC                     8

#define PO13_PIN_I2C_SCL                7
#define PO13_PIN_I2C_SDA                6

#define PO13_PIN_SPI_SCK                10
#define PO13_PIN_SPI_MOSI               11

extern struct io_context *light_display_po13_setup_io_i2c(uint8_t port_id);
extern struct io_context *light_display_po13_setup_io_spi_4p(uint8_t port_id);
extern struct io_context *light_display_po13_setup_io_spi_3p(uint8_t port_id);

extern struct display_device *light_display_po13_create_device(uint8_t *name, struct io_context *io);

#endif
