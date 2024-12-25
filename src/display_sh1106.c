#include <light_display_sh1106.h>

#include "light_display_sh1106_internal.h"

struct sh1106_state {
        struct io_context *io_ctx;
        uint8_t addrmode;
        uint8_t page_address;
        uint8_t column_address;
        uint16_t n_pages;
        uint16_t n_columns;
};

static struct display_driver_context *_sh1106_spawn_context();
static void _sh1106_init(struct display_device *dev);
static void _sh1106_reset(struct display_device *dev);
static void _sh1106_clear(struct display_device *dev, uint8_t value);
static void _sh1106_update(struct display_device *dev);

static struct display_driver _driver_sh1106 = {
        .name = "display.driver:sh1106",
        .spawn_context = _sh1106_spawn_context,
        .init_device = _sh1106_init,
        .reset = _sh1106_reset,
        .clear = _sh1106_clear,
        .update = _sh1106_update
};

struct display_driver *light_display_driver_sh1106()
{
        return &_driver_sh1106;
}

static struct display_driver_context *_sh1106_spawn_context()
{
        struct display_driver_context *ctx = light_alloc(sizeof(struct display_driver_context));
        ctx->driver = light_display_driver_sh1106();
        ctx->state = light_alloc(sizeof(struct sh1106_state));
        return ctx;
}

static void _sh1106_init(struct display_device *dev)
{
        // ASSERT dev->driver_ctx->driver == light_display_driver_sh1007()
        light_display_sh1106_chip_setup(dev);
}
static void _sh1106_reset(struct display_device *dev)
{
        // ASSERT dev->driver_ctx->driver == light_display_driver_sh1007()
        light_display_sh1106_reset_device(dev);
}
static void _sh1106_clear(struct display_device *dev, uint8_t value)
{
        light_display_sh1106_clear_screen(dev, value);
}
static void _sh1106_update(struct display_device *dev)
{
        light_display_sh1106_update_screen(dev);
}

void light_display_sh1106_reset_device(struct display_device *dev)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_signal_reset(state->io_ctx);
        state->n_pages = light_display_sh1106_y_to_pages(dev->height);
        state->n_columns = light_display_sh1106_x_to_columns(dev->width);
        state->addrmode = SH1106_ADDRMODE_VERTICAL;
        state->column_address = 0xFF;
        state->page_address = 0xFF;
}
void light_display_sh1106_chip_setup(struct display_device *dev)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_sh1106_reset_device(dev);
        light_display_sh1106_command_set_display_on(dev, false);        // display OFF
        light_display_sh1106_command_set_column_addr(dev, state->column_address);         // set column address (0x0)
        light_display_sh1106_command_set_page_addr(dev, state->page_address);           // set page address (0x0)
        light_display_sh1106_command_set_start_line(dev, 0x0);          // set display start line (0)
        light_display_sh1106_command_set_contrast(dev, 128);            // set contrast level (128)
        light_display_sh1106_command_set_addrmode(dev, state->addrmode);       // RAM addressing mode (vertical)
        light_display_sh1106_command_set_segment_remap(dev, false);     // set segment remap OFF
        light_display_sh1106_command_set_scan_dir(dev, SH1106_SCAN_DIR_DOWN);   // set common scan direction (down)
        light_display_sh1106_command_set_force_on(dev, false);          // set force all pixels (disable)
        light_display_sh1106_command_set_reverse_display(dev, false);   // set reverse mode OFF
        light_display_sh1106_command_set_multiplex_ratio(dev, 63);     // set multiplex ratio (1:64)
        light_display_sh1106_command_set_display_offset(dev, 96);       // set display offset (48)
        light_display_sh1106_command_set_display_clock(dev, 4, 1);      // set oscillator freq ([f-5%]/2)
        light_display_sh1106_command_set_charge_periods(dev, 2, 2);     // set pre-charge (2), dis-charge (2)
        light_display_sh1106_command_set_vcom_deselect(dev, 0x35);      // set VcomH (0.770 x Vref)
        light_display_sh1106_command_set_power_mode(dev, false, 5);     // set built-in DC-DC OFF
        light_display_sh1106_command_set_display_on(dev, true);         // display ON
}
void light_display_sh1106_clear_screen(struct display_device *dev, uint8_t value)
{
        // ASSERT dev->driver_ctx->driver === &_driver_sh1106
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_sh1106_command_set_page_addr(dev, 0);
        for(uint16_t column = 0; column < state->n_columns; column++) {
                light_display_sh1106_command_set_column_addr(dev, column);
                for(uint16_t page = 0; page < state->n_pages; page++) {
                        light_display_sh1106_write_data(dev, value);
                }
        }
}
void light_display_sh1106_update_screen(struct display_device *dev)
{
        // ASSERT dev->driver_ctx->driver === &_driver_sh1106
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;

        // write out each column page by page, starting with column 0.
        // page address increments automatically, and wraps at the end of each column
        light_display_sh1106_command_set_page_addr(dev, 0);
        for(uint16_t column = 0; column < state->n_columns; column++) {
                light_display_sh1106_command_set_column_addr(dev, column);
                for(uint16_t page = 0; page < state->n_pages; page++) {
                        light_display_sh1106_write_data(dev, dev->render_ctx->buffer[column * state->n_pages + page]);
                }
        }
}
struct display_device *light_display_sh1106_create_device(uint8_t *name, uint16_t width, uint16_t height, uint8_t bpp, struct io_context *io)
{
        // TODO validate *io
        struct display_device *dev = light_display_create_device(light_display_driver_sh1106(), width, height, bpp, name);
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        state->io_ctx = io;

        return dev;    
}

void light_display_sh1106_command_set_column_addr(struct display_device *dev, uint8_t column)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        if((state->column_address & 0xF0) != (column & 0xF0))
                light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_COL_ADDR_HIGH + (column >> 4));
        if((state->column_address & 0x0F) != (column & 0x0F))
                light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_COL_ADDR_LOW + (column & 0x0F));
        state->column_address = column;
}
void light_display_sh1106_command_set_addrmode(struct display_device *dev, uint8_t mode)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_ADDRMODE + mode);
}
void light_display_sh1106_command_set_contrast(struct display_device *dev, uint8_t level)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_CONTRAST);
        light_display_ioport_send_command_byte(state->io_ctx, level);
}
void light_display_sh1106_command_set_segment_remap(struct display_device *dev, bool enable)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_SEG_REMAP + enable);
}
extern void light_display_sh1106_command_set_multiplex_ratio(struct display_device *dev, uint8_t ratio)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_MUX_RATIO);
        light_display_ioport_send_command_byte(state->io_ctx, ratio);
}
void light_display_sh1106_command_set_force_on(struct display_device *dev, bool enable)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_FORCE_ON + enable);
}
void light_display_sh1106_command_set_reverse_display(struct display_device *dev, bool enable)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_REVERSE + enable);
}
void light_display_sh1106_command_set_display_offset(struct display_device *dev, uint8_t data)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_DISPLAY_OFFSET);
        light_display_ioport_send_command_byte(state->io_ctx, data);
}
void light_display_sh1106_command_set_power_mode(struct display_device *dev, bool enable, uint8_t mode)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_POWER_MODE);
        light_display_ioport_send_command_byte(state->io_ctx, 0x80 + enable + (mode << 1));
}
void light_display_sh1106_command_set_display_on(struct display_device *dev, bool enable)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_DISPLAY_ON + enable);
}
void light_display_sh1106_command_set_page_addr(struct display_device *dev, uint8_t page)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        if(state->page_address != page)
                light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_PAGE_ADDR + page);
        state->page_address = page;
}
void light_display_sh1106_command_set_scan_dir(struct display_device *dev, uint8_t data)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_SCAN_DIR + data);
}
void light_display_sh1106_command_set_display_clock(struct display_device *dev, uint8_t freq, uint8_t div)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_DISPLAY_CLK);
        light_display_ioport_send_command_byte(state->io_ctx, ((freq & 0x0F) << 4) + (div & 0x0F));
}
void light_display_sh1106_command_set_charge_periods(struct display_device *dev, uint8_t pre, uint8_t dis)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_CHARGE_PERIODS);
        light_display_ioport_send_command_byte(state->io_ctx, (pre & 0x0F) + ((dis & 0x0F) << 4));
}
void light_display_sh1106_command_set_vcom_deselect(struct display_device *dev, uint8_t data)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_VCOMH);
        light_display_ioport_send_command_byte(state->io_ctx, data);
}
void light_display_sh1106_command_set_start_line(struct display_device *dev, uint8_t addr)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_DISPLAY_START);
        light_display_ioport_send_command_byte(state->io_ctx, addr);
}
void light_display_sh1106_command_rmw_begin(struct display_device *dev)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_RMW_BEGIN);
}
void light_display_sh1106_command_rmw_end(struct display_device *dev)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_RMW_END);
}
void light_display_sh1106_command_no_op(struct display_device *dev)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1106_CMD_NOP);
}
void light_display_sh1106_write_data(struct display_device *dev, uint8_t data)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_display_ioport_send_data_byte(state->io_ctx, data);

        // increment address counter, to track state of driver's internal counter
        switch (state->addrmode)
        {
        case SH1106_ADDRMODE_PAGE:
                state->column_address = (state->column_address + 1) % state->n_columns;
                break;
        case SH1106_ADDRMODE_VERTICAL:
                state->page_address = (state->page_address + 1) % state->n_pages;
                break;
        }
}
uint16_t light_display_sh1106_y_to_pages(uint16_t y)
{
        return (y / 8) + ((y % 8)? 1 : 0);
}
uint16_t light_display_sh1106_x_to_columns(uint16_t x)
{
        return x;
}
