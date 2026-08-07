#include <light_display_st7789.h>
#include <light_platform.h>

#include "light_display_st7789_internal.h"

// generous upper bound for a stack-allocated single-row fill buffer in clear_screen() --
// covers this chip family's largest native panel width (240x320), not just this board's
// actual 240x280
#define ST7789_MAX_CLEAR_ROW_PIXELS     320

// upper bound on how long one update should ever take before it's considered stuck. worst
// case is bus-bound: a full 240x280x2 = 134400 byte frame at the SPI_BAUDRATE this
// platform's ioport uses (10MHz) is ~108ms; 500ms leaves ample headroom above that for a
// slower clock divisor or bus contention
#define ST7789_ASYNC_TIMEOUT_MS         500
// only relevant for a region narrower than the panel, which is chunked one row per burst
// (see _st7789_async_kick()) -- a full-width update is a single chunk regardless. rows are
// small enough that yielding after each would waste a scheduler tick apiece
#define ST7789_CHUNKS_PER_POLL          8

struct st7789_state {
        struct io_context *io_ctx;
        // driver-level preference, not chip state -- see light_display_st7789_set_offset()
        uint16_t col_offset;
        uint16_t row_offset;
};

static struct display_driver_context *_st7789_spawn_context();
static void _st7789_init(struct display_device *dev);
static void _st7789_reset(struct display_device *dev);
static void _st7789_clear(struct display_device *dev, uint16_t value);
static uint16_t _st7789_async_chunk_count(struct display_device *dev);
static void _st7789_async_kick(struct display_device *dev, uint16_t chunk_index);
static bool _st7789_async_chunk_complete(struct display_device *dev);

// a full-width region is one contiguous run in the render buffer, so it's a single chunk
// however tall it is (including the whole-frame case). only a narrower region has to be
// chunked, one row per burst, because its rows aren't adjacent in memory
static struct display_driver _driver_st7789 = {
        .name = "display.driver:st7789",
        .spawn_context = _st7789_spawn_context,
        .init_device = _st7789_init,
        .reset = _st7789_reset,
        .clear = _st7789_clear,
        .async_chunk_count = _st7789_async_chunk_count,
        .async_kick = _st7789_async_kick,
        .async_chunk_complete = _st7789_async_chunk_complete,
        .async_timeout_ms = ST7789_ASYNC_TIMEOUT_MS,
        .async_chunks_per_poll = ST7789_CHUNKS_PER_POLL
};

struct display_driver *light_display_driver_st7789()
{
        return &_driver_st7789;
}

static struct display_driver_context *_st7789_spawn_context()
{
        struct display_driver_context *ctx = light_alloc(sizeof(struct display_driver_context));
        ctx->driver = light_display_driver_st7789();
        ctx->state = light_alloc(sizeof(struct st7789_state));
        // light_alloc() is a plain malloc(), not zeroed -- must be set explicitly
        struct st7789_state *state = (struct st7789_state *) ctx->state;
        state->col_offset = ST7789_COL_OFFSET_DEFAULT;
        state->row_offset = ST7789_ROW_OFFSET_DEFAULT;
        return ctx;
}

static void _st7789_init(struct display_device *dev)
{
        light_display_st7789_chip_setup(dev);
}
static void _st7789_reset(struct display_device *dev)
{
        light_display_st7789_reset_device(dev);
}
static void _st7789_clear(struct display_device *dev, uint16_t value)
{
        light_display_st7789_clear_screen(dev, value);
}

void light_display_st7789_set_offset(struct display_device *dev, uint16_t col_offset, uint16_t row_offset)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        state->col_offset = col_offset;
        state->row_offset = row_offset;
}

void light_display_st7789_reset_device(struct display_device *dev)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        light_ioport_signal_reset(state->io_ctx);
}

void light_display_st7789_chip_setup(struct display_device *dev)
{
        light_display_st7789_reset_device(dev);
        light_display_st7789_command_sw_reset(dev);
        light_platform_sleep_ms(150);          // datasheet: >=120ms after SWRESET
        light_display_st7789_command_sleep_out(dev);
        light_platform_sleep_ms(120);          // datasheet: >=120ms after SLPOUT
        light_display_st7789_command_set_colmod(dev, ST7789_COLMOD_16BPP);
        // default orientation/RGB order -- MADCTL bits are display-mounting-specific and
        // expected to need an empirical correction pass, same as every other display
        // bring-up in this project
        light_display_st7789_command_set_madctl(dev, 0x00);
        // this board's panel showed inverted colors (background rendered white instead of
        // black) with inversion off -- panel-specific LC polarity, not a datasheet default
        light_display_st7789_command_set_inversion(dev, true);
        light_display_st7789_command_set_display_on(dev, true);
        light_platform_sleep_ms(20);
}

void light_display_st7789_clear_screen(struct display_device *dev, uint16_t color)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        light_display_st7789_command_set_window(dev, 0, 0, dev->width - 1, dev->height - 1);
        light_display_st7789_command_ram_write(dev);

        uint8_t row_buf[ST7789_MAX_CLEAR_ROW_PIXELS * 2];
        uint16_t n = dev->width < ST7789_MAX_CLEAR_ROW_PIXELS ? dev->width : ST7789_MAX_CLEAR_ROW_PIXELS;
        uint8_t hi = (uint8_t)(color >> 8);
        uint8_t lo = (uint8_t)(color & 0xFF);
        for(uint16_t i = 0; i < n; i++) {
                row_buf[i * 2]     = hi;
                row_buf[i * 2 + 1] = lo;
        }
        // one burst per row -- simple and correct; unlike the OLED drivers there's no
        // per-byte bit-transpose needed here, this is purely a "how big a single stack
        // buffer is reasonable" chunking choice
        for(uint16_t y = 0; y < dev->height; y++) {
                light_ioport_send_data_burst(state->io_ctx, row_buf, n * 2);
        }
}

// rend's 16bpp buffer is row-major RGB565 big-endian, matching ST7789's native RAMWR
// streaming order exactly -- unlike the 1bpp OLED drivers no per-pixel reassembly is
// needed, so a run of pixels can be handed to the transport straight from the render
// buffer. that only holds for a run that is contiguous in memory, which a region's rows
// are only when it spans the full panel width
static bool _region_is_full_width(struct display_device *dev)
{
        return dev->update_region.x0 == 0 && dev->update_region.x1 == dev->width - 1;
}
static uint16_t _region_rows(struct display_device *dev)
{
        return dev->update_region.y1 - dev->update_region.y0 + 1;
}
static uint16_t _px_bytes(struct display_device *dev)
{
        return (dev->bpp + 7) / 8;
}
static uint16_t _st7789_async_chunk_count(struct display_device *dev)
{
        return _region_is_full_width(dev) ? 1 : _region_rows(dev);
}
static void _st7789_async_kick(struct display_device *dev, uint16_t chunk_index)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        const struct display_region *r = &dev->update_region;
        uint16_t px = _px_bytes(dev);

        // the write window is armed once, on the first chunk only: ST7789 streams RAMWR
        // data into the window rectangle and wraps from x1 back to x0 on each new row by
        // itself, so every later row is just more data -- no per-row re-addressing
        if(chunk_index == 0) {
                light_display_st7789_command_set_window(dev, r->x0, r->y0, r->x1, r->y1);
                light_display_st7789_command_ram_write(dev);
        }

        if(_region_is_full_width(dev)) {
                uint32_t offset = (uint32_t)r->y0 * dev->width * px;
                uint32_t len = (uint32_t)_region_rows(dev) * dev->width * px;
                light_ioport_send_data_burst_async(state->io_ctx, dev->update_source_buffer + offset, len);
                return;
        }
        uint16_t y = r->y0 + chunk_index;
        uint32_t offset = ((uint32_t)y * dev->width + r->x0) * px;
        uint32_t len = (uint32_t)(r->x1 - r->x0 + 1) * px;
        light_ioport_send_data_burst_async(state->io_ctx, dev->update_source_buffer + offset, len);
}
static bool _st7789_async_chunk_complete(struct display_device *dev)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        return light_ioport_burst_is_complete(state->io_ctx);
}

struct display_device *light_display_st7789_create_device(uint8_t *name, uint16_t width, uint16_t height, struct io_context *io)
{
        // io_ctx must be attached to the driver state before the device is registered:
        // adding it to the object tree (via light_display_init_device()) immediately
        // triggers init_device()/reset(), which read state->io_ctx -- see the identical
        // pattern/rationale in light_display_sh1106_create_device()
        struct display_device *dev = light_object_alloc(sizeof(struct display_device));
        struct display_driver_context *driver_ctx = _st7789_spawn_context();
        struct st7789_state *state = (struct st7789_state *) driver_ctx->state;
        state->io_ctx = io;

        return light_display_init_device(dev, driver_ctx, width, height, 16, "%s", name);
}

void light_display_st7789_command_sw_reset(struct display_device *dev)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, ST7789_CMD_SWRESET);
}
void light_display_st7789_command_sleep_out(struct display_device *dev)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, ST7789_CMD_SLPOUT);
}
void light_display_st7789_command_set_colmod(struct display_device *dev, uint8_t format)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, ST7789_CMD_COLMOD);
        light_ioport_send_data_byte(state->io_ctx, format);
}
void light_display_st7789_command_set_madctl(struct display_device *dev, uint8_t bits)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, ST7789_CMD_MADCTL);
        light_ioport_send_data_byte(state->io_ctx, bits);
}
void light_display_st7789_command_set_inversion(struct display_device *dev, bool enable)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, enable ? ST7789_CMD_INVON : ST7789_CMD_INVOFF);
}
void light_display_st7789_command_set_display_on(struct display_device *dev, bool enable)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, enable ? ST7789_CMD_DISPON : ST7789_CMD_DISPOFF);
}
void light_display_st7789_command_set_window(struct display_device *dev,
        uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        uint16_t cx0 = x0 + state->col_offset;
        uint16_t cx1 = x1 + state->col_offset;
        uint16_t cy0 = y0 + state->row_offset;
        uint16_t cy1 = y1 + state->row_offset;

        light_ioport_send_command_byte(state->io_ctx, ST7789_CMD_CASET);
        uint8_t caset[4] = { (uint8_t)(cx0 >> 8), (uint8_t)(cx0 & 0xFF), (uint8_t)(cx1 >> 8), (uint8_t)(cx1 & 0xFF) };
        light_ioport_send_data_burst(state->io_ctx, caset, 4);

        light_ioport_send_command_byte(state->io_ctx, ST7789_CMD_RASET);
        uint8_t raset[4] = { (uint8_t)(cy0 >> 8), (uint8_t)(cy0 & 0xFF), (uint8_t)(cy1 >> 8), (uint8_t)(cy1 & 0xFF) };
        light_ioport_send_data_burst(state->io_ctx, raset, 4);
}
void light_display_st7789_command_ram_write(struct display_device *dev)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, ST7789_CMD_RAMWR);
}
