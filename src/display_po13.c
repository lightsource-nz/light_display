#include <light_display_po13.h>

// I/O port abstractions:
//      for now we just hard-code mappings for RP2040 targets.
//      TODO add a decoupling layer to support other targets

struct io_context *light_display_po13_setup_io_i2c(uint8_t port_id)
{
        return light_ioport_setup_io_i2c(
                port_id, PO13_PIN_RESET, PO13_I2C_ADDR, PO13_PIN_I2C_SCL, PO13_PIN_I2C_SDA);
}
struct io_context *light_display_po13_setup_io_spi_4p(uint8_t port_id)
{
        return light_ioport_setup_io_spi_4p(
                port_id, PO13_PIN_RESET, PO13_PIN_CS, PO13_PIN_DC,
                                PO13_PIN_SPI_SCK, PO13_PIN_SPI_MOSI);
}
struct io_context *light_display_po13_setup_io_pio_spi_4p(uint8_t port_id)
{
        return light_ioport_setup_io_pio_spi_4p(
                port_id, PO13_PIN_RESET, PO13_PIN_CS, PO13_PIN_DC,
                                PO13_PIN_SPI_SCK, PO13_PIN_SPI_MOSI);
}
struct io_context *light_display_po13_setup_io_spi_3p(uint8_t port_id)
{
        return light_ioport_setup_io_spi_3p(
                port_id, PO13_PIN_RESET, PO13_PIN_CS, PO13_PIN_SPI_SCK, PO13_PIN_SPI_MOSI);
}

struct display_device *light_display_po13_create_device(uint8_t *name, struct io_context *io)
{
        return light_display_sh1107_create_device(name, PO13_WIDTH, PO13_HEIGHT, PO13_BPP, io);
}
