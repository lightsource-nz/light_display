#include <light_display_ssd1322.h>
#include <light_platform.h>

//   a 256x64 panel at 4bpp is 8192 bytes, ~6.5ms at the 10MHz this SPI runs -- so 500ms is
// ample, and it is deliberately the same figure the other SPI display drivers here use rather
// than a second number invented for the same job
#define SSD1322_ASYNC_TIMEOUT_MS        500
//   rows are 128 bytes, which at 10MHz is ~100us: far too short to be worth yielding a
// scheduler tick over, and yielding on each would advance one row per tick. Batched for the
// same reason the SSD1351 batches its row case
#define SSD1322_ROW_CHUNKS_PER_POLL     8
// RES# low then a settle before the first command; nothing waits on this, it happens once
#define SSD1322_RESET_SETTLE_MS         10

struct ssd1322_state {
        struct io_context *io_ctx;
        //   ONE row of 4bpp pixels, converted from the 1bpp source just before it is sent.
        // The other drivers here DMA update_source_buffer straight to the wire because their
        // framebuffer format is already the controller's; this one cannot, because 1bpp in and
        // 4bpp out are different sizes as well as different layouts.
        //   a single buffer is safe despite the transfer being asynchronous: light_display
        // only kicks the next chunk once async_chunk_complete() has reported the previous one
        // done, so the conversion for row N+1 cannot overwrite a row N still in flight
        uint8_t *line;
        uint16_t line_len;
        //   where this panel's window sits in the controller's 480-pixel RAM, in COLUMN
        // ADDRESSES (4 pixels each) -- see the header for why this is not a constant
        uint8_t col_offset;
};

static struct display_driver_context *_ssd1322_spawn_context();
static void _ssd1322_destroy_context(struct display_driver_context *ctx);
static void _ssd1322_init(struct display_device *dev);
static void _ssd1322_reset(struct display_device *dev);
static void _ssd1322_clear(struct display_device *dev, uint16_t value);
static uint16_t _ssd1322_async_chunk_count(struct display_device *dev);
static uint16_t _ssd1322_async_chunks_per_poll(struct display_device *dev);
static void _ssd1322_async_kick(struct display_device *dev, uint16_t chunk_index);
static bool _ssd1322_async_chunk_complete(struct display_device *dev);

static struct display_driver _driver_ssd1322 = {
        .name = "display.driver:ssd1322",
        .spawn_context = _ssd1322_spawn_context,
        .destroy_context = _ssd1322_destroy_context,
        .init_device = _ssd1322_init,
        .reset = _ssd1322_reset,
        .clear = _ssd1322_clear,
        .async_chunk_count = _ssd1322_async_chunk_count,
        .async_kick = _ssd1322_async_kick,
        .async_chunk_complete = _ssd1322_async_chunk_complete,
        .async_timeout_ms = SSD1322_ASYNC_TIMEOUT_MS,
        .async_chunks_per_poll = _ssd1322_async_chunks_per_poll
};

struct display_driver *light_display_driver_ssd1322()
{
        return &_driver_ssd1322;
}

static struct display_driver_context *_ssd1322_spawn_context()
{
        struct display_driver_context *ctx = light_alloc(sizeof(struct display_driver_context));
        ctx->driver = light_display_driver_ssd1322();
        ctx->state = light_alloc(sizeof(struct ssd1322_state));
        // light_alloc() is a plain malloc(), not zeroed -- set explicitly, and the line buffer
        // especially: create_device() allocates it once the width is known
        struct ssd1322_state *state = (struct ssd1322_state *) ctx->state;
        state->io_ctx = NULL;
        state->line = NULL;
        state->line_len = 0;
        state->col_offset = 0;
        return ctx;
}
static void _ssd1322_destroy_context(struct display_driver_context *ctx)
{
        struct ssd1322_state *state = (struct ssd1322_state *) ctx->state;
        // the line buffer belongs to the context, so it goes with it
        if(state->line)
                light_free(state->line);
        light_free((void *)ctx->state);
        light_free(ctx);
}

static void _ssd1322_init(struct display_device *dev)
{
        light_display_ssd1322_chip_setup(dev);
}
static void _ssd1322_reset(struct display_device *dev)
{
        light_display_ssd1322_reset_device(dev);
}
static void _ssd1322_clear(struct display_device *dev, uint16_t value)
{
        light_display_ssd1322_clear_screen(dev, value);
}

static void _cmd(struct display_device *dev, uint8_t cmd, const uint8_t *args, uint8_t len)
{
        struct ssd1322_state *state = (struct ssd1322_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, cmd);
        if(len)
                light_ioport_send_data_burst(state->io_ctx, args, len);
}

void light_display_ssd1322_reset_device(struct display_device *dev)
{
        struct ssd1322_state *state = (struct ssd1322_state *) dev->driver_ctx->state;
        //   a no-op on a board whose RES# is tied to its own rail rather than a GPIO
        // (LIGHT_IOPORT_PIN_NONE); light_ioport skips the pulse there. This part has no
        // software-reset command, so such a board depends entirely on chip_setup() below
        // putting every register into a known state
        light_ioport_signal_reset(state->io_ctx);
        light_platform_sleep_ms(SSD1322_RESET_SETTLE_MS);
}

void light_display_ssd1322_chip_setup(struct display_device *dev)
{
        light_display_ssd1322_reset_device(dev);

        //   UNLOCK FIRST. Most of what follows is in the locked command set and is accepted
        // and ignored until this lands, which presents as a panel that responds to nothing
        _cmd(dev, SSD1322_CMD_COMMAND_LOCK, (const uint8_t[]){ SSD1322_UNLOCK_COMMANDS }, 1);
        _cmd(dev, SSD1322_CMD_DISPLAY_OFF, NULL, 0);

        _cmd(dev, SSD1322_CMD_CLOCK_DIVIDER, (const uint8_t[]){ 0x91 }, 1);
        //   MUX ratio is height-1: this is the one init value that must track the panel
        // rather than being copied from a reference sequence. A 64-row panel left at a 128-row
        // ratio drives rows that do not exist and dims the ones that do
        _cmd(dev, SSD1322_CMD_SET_MUX_RATIO, (const uint8_t[]){ (uint8_t)(dev->height - 1) }, 1);
        _cmd(dev, SSD1322_CMD_SET_DISPLAY_OFFSET, (const uint8_t[]){ 0x00 }, 1);
        _cmd(dev, SSD1322_CMD_SET_START_LINE, (const uint8_t[]){ 0x00 }, 1);
        _cmd(dev, SSD1322_CMD_SET_REMAP,
                        (const uint8_t[]){ SSD1322_REMAP_DEFAULT_LOW, SSD1322_REMAP_DEFAULT_HIGH }, 2);

        _cmd(dev, SSD1322_CMD_SET_GPIO, (const uint8_t[]){ 0x00 }, 1);
        //   0x01 selects the INTERNAL VDD regulator. A module with its own boost -- the
        // inductor on the board is the tell -- still wants this: the regulator being selected
        // is the controller's internal logic supply, not the panel's high rail
        _cmd(dev, SSD1322_CMD_FUNCTION_SELECT, (const uint8_t[]){ 0x01 }, 1);
        _cmd(dev, SSD1322_CMD_DISPLAY_ENHANCE_A,
                        (const uint8_t[]){ SSD1322_ENHANCE_A_VSL, 0xFD }, 2);

        _cmd(dev, SSD1322_CMD_CONTRAST_CURRENT, (const uint8_t[]){ SSD1322_CONTRAST_DEFAULT }, 1);
        _cmd(dev, SSD1322_CMD_MASTER_CONTRAST,
                        (const uint8_t[]){ SSD1322_MASTER_CONTRAST_DEFAULT }, 1);
        //   the linear default greyscale table. Worth sending explicitly rather than trusting
        // power-on state, and it is what makes the 0x0/0xF this driver writes read as fully
        // off and fully on rather than as two arbitrary points on a custom curve
        _cmd(dev, SSD1322_CMD_GREYSCALE_DEFAULT, NULL, 0);

        _cmd(dev, SSD1322_CMD_PHASE_LENGTH, (const uint8_t[]){ 0xE2 }, 1);
        _cmd(dev, SSD1322_CMD_PRECHARGE_VOLTAGE, (const uint8_t[]){ 0x1F }, 1);
        _cmd(dev, SSD1322_CMD_SECOND_PRECHARGE, (const uint8_t[]){ 0x08 }, 1);
        _cmd(dev, SSD1322_CMD_SET_VCOMH, (const uint8_t[]){ 0x07 }, 1);

        _cmd(dev, SSD1322_CMD_DISPLAY_NORMAL, NULL, 0);
        //   cleared BEFORE the panel is switched on. Display RAM comes up holding whatever it
        // holds, and turning on first shows that to the user as a flash of noise
        light_display_ssd1322_clear_screen(dev, 0);
        _cmd(dev, SSD1322_CMD_DISPLAY_ON, NULL, 0);
}

//   arms the RAM window. x is in PIXELS at this interface and converted to column addresses
// here, because a caller thinking in pixels is the whole point of the abstraction -- and
// because the conversion is where the panel's offset into the 480-pixel RAM belongs
void light_display_ssd1322_command_set_window(struct display_device *dev,
                uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
        struct ssd1322_state *state = (struct ssd1322_state *) dev->driver_ctx->state;
        uint8_t c0 = (uint8_t)(state->col_offset + x0 / SSD1322_PIXELS_PER_COLUMN);
        uint8_t c1 = (uint8_t)(state->col_offset + x1 / SSD1322_PIXELS_PER_COLUMN);

        _cmd(dev, SSD1322_CMD_SET_COLUMN, (const uint8_t[]){ c0, c1 }, 2);
        _cmd(dev, SSD1322_CMD_SET_ROW, (const uint8_t[]){ (uint8_t)y0, (uint8_t)y1 }, 2);
        _cmd(dev, SSD1322_CMD_WRITE_RAM, NULL, 0);
}

void light_display_ssd1322_clear_screen(struct display_device *dev, uint16_t value)
{
        struct ssd1322_state *state = (struct ssd1322_state *) dev->driver_ctx->state;
        //   any non-zero asks for lit pixels, and 4bpp full-on is 0xF in both nibbles. This
        // takes a uint16_t only because the driver interface does, for the benefit of 16bpp
        // drivers that need a real colour
        uint8_t fill = value ? 0xFF : 0x00;

        for(uint16_t i = 0; i < state->line_len; i++)
                state->line[i] = fill;

        light_display_ssd1322_command_set_window(dev, 0, 0, dev->width - 1, dev->height - 1);
        //   synchronous, and one row at a time reusing the same buffer: this runs at init and
        // from the clear command, never from the async path, so there is nothing to overlap
        // with and no reason to hold a whole frame of scratch for it
        for(uint16_t y = 0; y < dev->height; y++)
                light_ioport_send_data_burst(state->io_ctx, state->line, state->line_len);
}

//   one source pixel out of the 1bpp framebuffer. Row-major, ceil(width/8) bytes per row,
// and LSB-FIRST within each byte -- bit 0 is the leftmost pixel of its group, which is
// light_draw's packing and not the order one would guess
static inline bool _mono_pixel(const uint8_t *row, uint16_t x)
{
        return (row[x / 8] & (1 << (x % 8))) != 0;
}
//   expands one row of 1bpp source into the controller's 4bpp packing: two pixels per byte,
// HIGH nibble leftmost. Off becomes 0x0 and on becomes 0xF, so the panel's greyscale is being
// driven at its two extremes -- see the note at the top of the header
static void _convert_row(struct ssd1322_state *state, const uint8_t *row, uint16_t width)
{
        for(uint16_t i = 0; i < state->line_len; i++) {
                uint16_t x = (uint16_t)(i * SSD1322_PIXELS_PER_BYTE);
                uint8_t hi = (x < width && _mono_pixel(row, x)) ? 0xF0 : 0x00;
                uint8_t lo = (x + 1 < width && _mono_pixel(row, x + 1)) ? 0x0F : 0x00;
                state->line[i] = (uint8_t)(hi | lo);
        }
}

//   ALWAYS full-width rows, whatever region was invalidated. Column addressing on this part
// is in units of four pixels, so a partial-width window would have to be snapped outward to a
// 4-pixel boundary anyway -- and at 128 bytes a row, sending the whole row costs less than
// getting that alignment subtly wrong. The row range is still honoured, which is where the
// real saving is on a 64-row panel
static uint16_t _ssd1322_async_chunk_count(struct display_device *dev)
{
        return (uint16_t)(dev->update_region.y1 - dev->update_region.y0 + 1);
}
static uint16_t _ssd1322_async_chunks_per_poll(struct display_device *dev)
{
        return SSD1322_ROW_CHUNKS_PER_POLL;
}
static void _ssd1322_async_kick(struct display_device *dev, uint16_t chunk_index)
{
        struct ssd1322_state *state = (struct ssd1322_state *) dev->driver_ctx->state;
        const struct display_region *r = &dev->update_region;
        uint16_t y = (uint16_t)(r->y0 + chunk_index);

        //   the window is armed once, on the first chunk: RAM writes advance and wrap from the
        // window's right edge to its left by themselves, so every later row is just more data
        if(chunk_index == 0)
                light_display_ssd1322_command_set_window(dev, 0, r->y0, dev->width - 1, r->y1);

        uint16_t width_bytes = (uint16_t)((dev->width + 7) / 8);
        _convert_row(state, dev->update_source_buffer + (uint32_t)y * width_bytes, dev->width);
        light_ioport_send_data_burst_async(state->io_ctx, state->line, state->line_len);
}
static bool _ssd1322_async_chunk_complete(struct display_device *dev)
{
        struct ssd1322_state *state = (struct ssd1322_state *) dev->driver_ctx->state;
        return light_ioport_burst_is_complete(state->io_ctx);
}

void light_display_ssd1322_command_set_display_on(struct display_device *dev, bool enable)
{
        _cmd(dev, enable ? SSD1322_CMD_DISPLAY_ON : SSD1322_CMD_DISPLAY_OFF, NULL, 0);
}
void light_display_ssd1322_command_set_inversion(struct display_device *dev, bool enable)
{
        _cmd(dev, enable ? SSD1322_CMD_DISPLAY_INVERSE : SSD1322_CMD_DISPLAY_NORMAL, NULL, 0);
}
//   false returns to NORMAL rather than to entire-display-off, because the useful opposite of
// "light everything regardless of RAM" is "show RAM again" -- 0xA4 would blank the panel and
// look identical to the fault being diagnosed
void light_display_ssd1322_command_set_entire_on(struct display_device *dev, bool enable)
{
        _cmd(dev, enable ? SSD1322_CMD_ENTIRE_DISPLAY_ON : SSD1322_CMD_DISPLAY_NORMAL, NULL, 0);
}
//   re-sends Display Enhancement A with a different VSL selection, so a bring-up rig can try
// both without a rebuild. `vsl_select` is SSD1322_ENHANCE_A_VSL_EXTERNAL or _INTERNAL.
//   safe to call with the display running: it reconfigures where segment drive comes from and
// takes effect immediately, which is exactly what makes it testable by watching the panel
void light_display_ssd1322_set_vsl(struct display_device *dev, uint8_t vsl_select)
{
        _cmd(dev, SSD1322_CMD_DISPLAY_ENHANCE_A, (const uint8_t[]){ vsl_select, 0xFD }, 2);
}
void light_display_ssd1322_set_contrast(struct display_device *dev, uint8_t level)
{
        _cmd(dev, SSD1322_CMD_CONTRAST_CURRENT, &level, 1);
}
void light_display_ssd1322_set_master_contrast(struct display_device *dev, uint8_t level)
{
        if(level > SSD1322_MASTER_CONTRAST_MAX)
                level = SSD1322_MASTER_CONTRAST_MAX;
        _cmd(dev, SSD1322_CMD_MASTER_CONTRAST, &level, 1);
}

struct display_device *light_display_ssd1322_create_device(
                uint8_t *name, uint16_t width, uint16_t height, struct io_context *io)
{
        struct display_device *dev = light_object_alloc(sizeof(struct display_device));
        struct display_driver_context *driver_ctx = _ssd1322_spawn_context();
        struct ssd1322_state *state = (struct ssd1322_state *) driver_ctx->state;

        //   everything the init path reads must be in place BEFORE the device joins the object
        // tree, because that add triggers init_device() synchronously. For this driver that
        // includes the line buffer as well as the io_context: chip_setup() clears the screen
        // through it, so a NULL there would fault during registration
        state->io_ctx = io;
        state->line_len = (uint16_t)(width / SSD1322_PIXELS_PER_BYTE);
        state->line = light_alloc(state->line_len);
        //   the panel's window is centred in the controller's RAM -- see the header. Computed
        // rather than hard-coded so a 480- or 128-wide panel gets 0 and a 256 gets 28
        state->col_offset = (uint8_t)(((SSD1322_RAM_WIDTH - width) / 2) / SSD1322_PIXELS_PER_COLUMN);

        //   1bpp, not 4: this driver expands mono to the controller's greyscale on the way
        // out, so the framebuffer light_draw hands it is a mono one
        return light_display_init_device(dev, driver_ctx, width, height, 1, "%s", name);
}
