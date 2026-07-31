#ifndef _LIGHT_DISPLAY_PO13_H
#define _LIGHT_DISPLAY_PO13_H

#include <light_display_sh1107.h>

#include <stdint.h>

#define PO13_WIDTH                      64
#define PO13_HEIGHT                     128
#define PO13_BPP                        1

#define PO13_PIN_RESET                  12
#define PO13_PIN_CS                     9
#define PO13_PIN_DC                     8

#define PO13_PIN_I2C_SCL                7
#define PO13_PIN_I2C_SDA                6
// common default 7-bit I2C address for this controller family (SA0/D-C# pin tied low)
#define PO13_I2C_ADDR                   0x3C

#define PO13_PIN_SPI_SCK                10
#define PO13_PIN_SPI_MOSI               11

extern struct io_context *light_display_po13_setup_io_i2c(uint8_t port_id);
extern struct io_context *light_display_po13_setup_io_spi_4p(uint8_t port_id);
extern struct io_context *light_display_po13_setup_io_spi_3p(uint8_t port_id);

extern struct display_device *light_display_po13_create_device(uint8_t *name, struct io_context *io);

#endif
