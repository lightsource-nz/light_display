#include <light_display_st7789.h>
#include <light_platform.h>

#include "light_display_st7789_internal.h"

// generous upper bound for a stack-allocated single-row fill buffer in clear_screen() --
// covers this chip family's largest native panel width (240x320), not just this board's
// actual 240x280
#define ST7789_MAX_CLEAR_ROW_PIXELS     320

// upper bound on how long one async update (the whole frame, one contiguous burst --
// unlike the OLED drivers, there's no per-column chunking here) should ever take before
// it's considered stuck. worst case is bus-bound: 240x280x2 = 134400 bytes at the
// SPI_BAUDRATE this platform's ioport uses (10MHz) is ~108ms; 500ms leaves ample headroom
// above that for a slower clock divisor or bus contention
#define ST7789_ASYNC_TIMEOUT_MS         500

struct st7789_state {
        struct io_context *io_ctx;
        // driver-level preference, not chip state -- see light_display_st7789_set_offset()
        uint16_t col_offset;
        uint16_t row_offset;
        // async update state -- see _st7789_update_async_start()/_poll() below. unlike the
        // OLED drivers there's no per-column/page state machine to track: the whole frame
        // is one contiguous burst, so this is just "is it in flight, and since when"
        bool update_in_progress;
        uint32_t update_start_time_ms;
};

static struct display_driver_context *_st7789_spawn_context();
static void _st7789_init(struct display_device *dev);
static void _st7789_reset(struct display_device *dev);
static void _st7789_clear(struct display_device *dev, uint16_t value);
static void _st7789_update(struct display_device *dev);
static void _st7789_update_async_start(struct display_device *dev);
static bool _st7789_update_async_poll(struct display_device *dev);
static bool _st7789_update_async_is_active(struct display_device *dev);

static struct display_driver _driver_st7789 = {
        .name = "display.driver:st7789",
        .spawn_context = _st7789_spawn_context,
        .init_device = _st7789_init,
        .reset = _st7789_reset,
        .clear = _st7789_clear,
        .update = _st7789_update,
        .update_async_start = _st7789_update_async_start,
        .update_async_poll = _st7789_update_async_poll,
        .update_async_is_active = _st7789_update_async_is_active
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
        state->update_in_progress = false;
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
static void _st7789_update(struct display_device *dev)
{
        light_display_st7789_update_screen(dev);
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
        light_display_ioport_signal_reset(state->io_ctx);
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
                light_display_ioport_send_data_burst(state->io_ctx, row_buf, n * 2);
        }
}

void light_display_st7789_update_screen(struct display_device *dev)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;

        // rend's buffer is already row-major RGB565, big-endian, matching ST7789's native
        // RAMWR streaming order exactly -- unlike the 1bpp OLED drivers, no per-pixel
        // reassembly is needed, the whole frame goes out as a single contiguous burst
        // straight from the render context's own buffer
        light_display_st7789_command_set_window(dev, 0, 0, dev->width - 1, dev->height - 1);
        light_display_st7789_command_ram_write(dev);
        light_display_ioport_send_data_burst(state->io_ctx, dev->render_ctx->buffer, dev->render_ctx->buffer_length);
}

// kicks off the whole frame as a single non-blocking DMA burst and returns immediately --
// unlike the OLED drivers, there's nothing to chunk: CASET/RASET/RAMWR are sent
// (blocking, but each is only a handful of bytes) to set up the write window, then the
// entire render buffer goes out as one light_display_ioport_send_data_burst_async() call.
// dev->render_ctx->buffer is read by DMA asynchronously after this returns -- safe because
// the generic light_display layer (_light_display_drain_async() in display.c) blocks any
// sync reset/clear/update on this device until update_async_is_active() reports false, and
// screentest_common's own app.c checks light_display_render_context_busy() before ever
// calling rend_context_swap_buffers(), so nothing overwrites this buffer while it's still
// being read
static void _st7789_update_async_start(struct display_device *dev)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        if(state->update_in_progress) {
                light_warn("update already in progress for device '%s', ignoring", dev->header.id);
                return;
        }
        state->update_in_progress = true;
        state->update_start_time_ms = light_platform_get_time_since_init();

        light_display_st7789_command_set_window(dev, 0, 0, dev->width - 1, dev->height - 1);
        light_display_st7789_command_ram_write(dev);
        light_display_ioport_send_data_burst_async(state->io_ctx, dev->render_ctx->buffer, dev->render_ctx->buffer_length);
}
// single non-blocking check, unlike SH1107's busy-drain-the-whole-sweep loop -- there's
// only one burst total here, not dozens of small per-column ones, so there's nothing to
// gain by draining harder than "check once and let the scheduler come back"
static bool _st7789_update_async_poll(struct display_device *dev)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        if(!state->update_in_progress)
                return true;

        if(light_platform_get_time_since_init() - state->update_start_time_ms > ST7789_ASYNC_TIMEOUT_MS) {
                light_error("async update timed out for device '%s', aborting", dev->header.id);
                state->update_in_progress = false;
                return true;
        }
        if(!light_display_ioport_burst_is_complete(state->io_ctx))
                return false;
        state->update_in_progress = false;
        return true;
}
static bool _st7789_update_async_is_active(struct display_device *dev)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        return state->update_in_progress;
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
        light_display_ioport_send_command_byte(state->io_ctx, ST7789_CMD_SWRESET);
}
void light_display_st7789_command_sleep_out(struct display_device *dev)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, ST7789_CMD_SLPOUT);
}
void light_display_st7789_command_set_colmod(struct display_device *dev, uint8_t format)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, ST7789_CMD_COLMOD);
        light_display_ioport_send_data_byte(state->io_ctx, format);
}
void light_display_st7789_command_set_madctl(struct display_device *dev, uint8_t bits)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, ST7789_CMD_MADCTL);
        light_display_ioport_send_data_byte(state->io_ctx, bits);
}
void light_display_st7789_command_set_inversion(struct display_device *dev, bool enable)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, enable ? ST7789_CMD_INVON : ST7789_CMD_INVOFF);
}
void light_display_st7789_command_set_display_on(struct display_device *dev, bool enable)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, enable ? ST7789_CMD_DISPON : ST7789_CMD_DISPOFF);
}
void light_display_st7789_command_set_window(struct display_device *dev,
        uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        uint16_t cx0 = x0 + state->col_offset;
        uint16_t cx1 = x1 + state->col_offset;
        uint16_t cy0 = y0 + state->row_offset;
        uint16_t cy1 = y1 + state->row_offset;

        light_display_ioport_send_command_byte(state->io_ctx, ST7789_CMD_CASET);
        uint8_t caset[4] = { (uint8_t)(cx0 >> 8), (uint8_t)(cx0 & 0xFF), (uint8_t)(cx1 >> 8), (uint8_t)(cx1 & 0xFF) };
        light_display_ioport_send_data_burst(state->io_ctx, caset, 4);

        light_display_ioport_send_command_byte(state->io_ctx, ST7789_CMD_RASET);
        uint8_t raset[4] = { (uint8_t)(cy0 >> 8), (uint8_t)(cy0 & 0xFF), (uint8_t)(cy1 >> 8), (uint8_t)(cy1 & 0xFF) };
        light_display_ioport_send_data_burst(state->io_ctx, raset, 4);
}
void light_display_st7789_command_ram_write(struct display_device *dev)
{
        struct st7789_state *state = (struct st7789_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, ST7789_CMD_RAMWR);
}
