#include <light_display_ssd1351.h>
#include <light_platform.h>

//   upper bound before a chunk in flight is considered stuck. A full 128x128x2 = 32768 byte
// frame at the 10MHz this part's SPI tops out at is ~26ms, so 500ms is ample headroom, and
// it is deliberately the same figure the ST7735 and ST7789 drivers use rather than a second
// number invented for the same purpose
#define SSD1351_ASYNC_TIMEOUT_MS        500
#define SSD1351_ROW_CHUNKS_PER_POLL     8

//   the reset pulse this part wants. The datasheet asks for RES# low for at least 2us and a
// settling time before the first command; light_ioport_signal_reset() supplies the pulse,
// and this is the settle. 10ms rather than the microseconds required, because nothing is
// waiting on it -- it happens once, at init
#define SSD1351_RESET_SETTLE_MS         10

//   per-channel contrast applied by chip_setup(). These are the values the vendor sample
// code for this panel family uses, and they are not equal on purpose: the blue segment of
// this OLED chemistry is the least efficient, so an equal drive renders white with a yellow
// cast. A board whose white point still looks wrong corrects it with set_contrast()
#define SSD1351_CONTRAST_A_DEFAULT      0xC8
#define SSD1351_CONTRAST_B_DEFAULT      0x80
#define SSD1351_CONTRAST_C_DEFAULT      0xC8

struct ssd1351_state {
        struct io_context *io_ctx;
        uint8_t remap;
};

static struct display_driver_context *_ssd1351_spawn_context();
static void _ssd1351_destroy_context(struct display_driver_context *ctx);
static void _ssd1351_init(struct display_device *dev);
static void _ssd1351_reset(struct display_device *dev);
static void _ssd1351_clear(struct display_device *dev, uint16_t value);
static uint16_t _ssd1351_async_chunk_count(struct display_device *dev);
static uint16_t _ssd1351_async_chunks_per_poll(struct display_device *dev);
static void _ssd1351_async_kick(struct display_device *dev, uint16_t chunk_index);
static bool _ssd1351_async_chunk_complete(struct display_device *dev);

static struct display_driver _driver_ssd1351 = {
        .name = "display.driver:ssd1351",
        .spawn_context = _ssd1351_spawn_context,
        .destroy_context = _ssd1351_destroy_context,
        .init_device = _ssd1351_init,
        .reset = _ssd1351_reset,
        .clear = _ssd1351_clear,
        .async_chunk_count = _ssd1351_async_chunk_count,
        .async_kick = _ssd1351_async_kick,
        .async_chunk_complete = _ssd1351_async_chunk_complete,
        .async_timeout_ms = SSD1351_ASYNC_TIMEOUT_MS,
        .async_chunks_per_poll = _ssd1351_async_chunks_per_poll
};

struct display_driver *light_display_driver_ssd1351()
{
        return &_driver_ssd1351;
}

static struct display_driver_context *_ssd1351_spawn_context()
{
        struct display_driver_context *ctx = light_alloc(sizeof(struct display_driver_context));
        ctx->driver = light_display_driver_ssd1351();
        ctx->state = light_alloc(sizeof(struct ssd1351_state));
        // light_alloc() is a plain malloc(), not zeroed -- must be set explicitly
        struct ssd1351_state *state = (struct ssd1351_state *) ctx->state;
        state->remap = SSD1351_REMAP_DEFAULT;
        return ctx;
}

//   the counterpart to _ssd1351_spawn_context(), called from the device release path when
// the device this context was spawned for is freed. Frees in the reverse of the order
// allocated: the state first, then the context that points at it
static void _ssd1351_destroy_context(struct display_driver_context *ctx)
{
        light_free((void *)ctx->state);
        light_free(ctx);
}

static void _ssd1351_init(struct display_device *dev)
{
        light_display_ssd1351_chip_setup(dev);
}
static void _ssd1351_reset(struct display_device *dev)
{
        light_display_ssd1351_reset_device(dev);
}
static void _ssd1351_clear(struct display_device *dev, uint16_t value)
{
        light_display_ssd1351_clear_screen(dev, value);
}

void light_display_ssd1351_set_remap(struct display_device *dev, uint8_t remap)
{
        struct ssd1351_state *state = (struct ssd1351_state *) dev->driver_ctx->state;
        state->remap = remap;
}

void light_display_ssd1351_reset_device(struct display_device *dev)
{
        struct ssd1351_state *state = (struct ssd1351_state *) dev->driver_ctx->state;
        //   a no-op on a board that wired RES# to its own reset rail rather than to a GPIO
        // (LIGHT_IOPORT_PIN_NONE) -- light_ioport skips the pulse there. Unlike ST7735 there
        // is no software-reset command to fall back on: this part has no SWRESET, so a board
        // with no reset line depends entirely on the full register init below putting the
        // controller into a known state
        light_ioport_signal_reset(state->io_ctx);
        light_platform_sleep_ms(SSD1351_RESET_SETTLE_MS);
}

//   a command byte plus its parameters, which is what most of an SSD1351 init is. Written as
// a helper because the sequence below is long enough that spelling out send_command/
// send_data at each step buries the values, which are the part worth reading
static void _cmd(struct display_device *dev, uint8_t cmd, const uint8_t *args, uint8_t len)
{
        struct ssd1351_state *state = (struct ssd1351_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, cmd);
        if(len)
                light_ioport_send_data_burst(state->io_ctx, args, len);
}

void light_display_ssd1351_chip_setup(struct display_device *dev)
{
        struct ssd1351_state *state = (struct ssd1351_state *) dev->driver_ctx->state;

        light_display_ssd1351_reset_device(dev);

        //   UNLOCK FIRST, and both halves of it. Everything from DISPLAYOFFSET down is in the
        // locked set, so the ordering here is not cosmetic -- see the note in the header
        _cmd(dev, SSD1351_CMD_COMMANDLOCK, (const uint8_t[]){ SSD1351_UNLOCK_COMMANDS }, 1);
        _cmd(dev, SSD1351_CMD_COMMANDLOCK, (const uint8_t[]){ SSD1351_UNLOCK_EXTENDED }, 1);

        // dark while the registers are being set, so a half-configured frame is never shown
        light_display_ssd1351_command_set_display_on(dev, false);

        // oscillator frequency in the high nibble, divide ratio in the low: 0xF1 is the
        // fastest oscillator with a divide of 2, which is what gives this panel its ~100Hz
        // frame rate. Slower settings are visibly flickery on an OLED
        _cmd(dev, SSD1351_CMD_CLOCKDIV, (const uint8_t[]){ 0xF1 }, 1);

        //   MUX RATIO tracks the panel height rather than being a constant, so the same
        // driver covers the 128x128 and 128x96 members of this family. The controller counts
        // COM lines from zero, hence height-1
        _cmd(dev, SSD1351_CMD_MUXRATIO, (const uint8_t[]){ (uint8_t)(dev->height - 1) }, 1);

        _cmd(dev, SSD1351_CMD_SETREMAP, &state->remap, 1);

        //   the addressable window, set once to the whole panel. Every update re-arms it, so
        // this exists only so that a clear or a stray RAM write before the first update lands
        // somewhere defined rather than wherever the previous power cycle left the pointer
        _cmd(dev, SSD1351_CMD_SETCOLUMN,
                        (const uint8_t[]){ 0x00, (uint8_t)(dev->width - 1) }, 2);
        _cmd(dev, SSD1351_CMD_SETROW,
                        (const uint8_t[]){ 0x00, (uint8_t)(dev->height - 1) }, 2);

        //   START LINE and DISPLAY OFFSET both zero, which is right for a panel whose glass
        // uses the full COM range -- the 128-row parts do. The 128x96 member of this family
        // is wired to the middle of that range and wants a start line of 0x60 instead; it is
        // left as a note rather than a computed special case because no such panel has been
        // driven here, and a rule written from one datasheet reading is worth less than one
        // written from a panel that was actually looked at
        _cmd(dev, SSD1351_CMD_STARTLINE, (const uint8_t[]){ 0x00 }, 1);
        _cmd(dev, SSD1351_CMD_DISPLAYOFFSET, (const uint8_t[]){ 0x00 }, 1);

        // the controller has two GPIOs of its own, unused on every board this has driven.
        // 0x00 disables both -- left floating they can leak into the panel supply
        _cmd(dev, SSD1351_CMD_SETGPIO, (const uint8_t[]){ 0x00 }, 1);
        //   internal VDD regulator. The alternative is an external regulator the module would
        // have to carry, and none of these breakout modules do -- with this set to 0x00 the
        // panel stays completely dark and looks like a wiring fault
        _cmd(dev, SSD1351_CMD_FUNCTIONSELECT, (const uint8_t[]){ 0x01 }, 1);

        // precharge phase 1 (low nibble) and phase 2 (high), then the second precharge
        // period and the precharge voltage. OLED drive timing rather than anything visible
        // in the image until it is wrong, when it shows as ghosting on high-contrast edges
        _cmd(dev, SSD1351_CMD_PRECHARGE, (const uint8_t[]){ 0x32 }, 1);
        _cmd(dev, SSD1351_CMD_PRECHARGE2, (const uint8_t[]){ 0x01 }, 1);
        _cmd(dev, SSD1351_CMD_PRECHARGELEVEL, (const uint8_t[]){ 0x17 }, 1);
        // VCOMH, the common-electrode deselect level
        _cmd(dev, SSD1351_CMD_VCOMH, (const uint8_t[]){ 0x05 }, 1);
        // segment low voltage, driven externally on these modules -- the fixed three-byte
        // sequence the datasheet gives for that arrangement
        _cmd(dev, SSD1351_CMD_SETVSL, (const uint8_t[]){ 0xA0, 0xB5, 0x55 }, 3);

        light_display_ssd1351_set_contrast(dev, SSD1351_CONTRAST_A_DEFAULT,
                        SSD1351_CONTRAST_B_DEFAULT, SSD1351_CONTRAST_C_DEFAULT);
        light_display_ssd1351_set_master_contrast(dev, SSD1351_MASTER_CONTRAST_MAX);

        //   INVERSION OFF, which is the opposite of what this project's ST7735 and ST7789
        // panels needed. An OLED pixel emits rather than gates a backlight, so its off state
        // is genuinely black and the controller default is already the right polarity --
        // there is no normally-white glass to compensate for here
        light_display_ssd1351_command_set_inversion(dev, false);

        light_display_ssd1351_command_set_display_on(dev, true);
        // datasheet: the panel needs time for its charge pump to come up before it will
        // render properly. Writing RAM sooner is harmless but the first frame can flicker
        light_platform_sleep_ms(100);
}

void light_display_ssd1351_clear_screen(struct display_device *dev, uint16_t color)
{
        struct ssd1351_state *state = (struct ssd1351_state *) dev->driver_ctx->state;
        light_display_ssd1351_command_set_window(dev, 0, 0, dev->width - 1, dev->height - 1);
        light_display_ssd1351_command_ram_write(dev);

        //   one row at a time out of a scratch buffer, rather than a whole frame: 128x128x2
        // is 32KB, which is not a thing to put on a stack. The window wraps by itself at the
        // end of each row, so the rows are just more data
        uint8_t row_buf[SSD1351_MAX_WIDTH * 2];
        uint16_t n = dev->width < SSD1351_MAX_WIDTH ? dev->width : SSD1351_MAX_WIDTH;
        uint8_t hi = (uint8_t)(color >> 8);
        uint8_t lo = (uint8_t)(color & 0xFF);
        for(uint16_t i = 0; i < n; i++) {
                row_buf[i * 2]     = hi;
                row_buf[i * 2 + 1] = lo;
        }
        for(uint16_t y = 0; y < dev->height; y++) {
                light_ioport_send_data_burst(state->io_ctx, row_buf, n * 2);
        }
}

// light_draw's 16bpp buffer is row-major RGB565 big-endian, which is exactly the order this
// controller consumes RAM writes in, so a run of pixels goes to the transport straight from
// the render buffer -- but only where that run is contiguous, which a region's rows are only
// at full panel width
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
static uint16_t _ssd1351_async_chunk_count(struct display_device *dev)
{
        return _region_is_full_width(dev) ? 1 : _region_rows(dev);
}
static uint16_t _ssd1351_async_chunks_per_poll(struct display_device *dev)
{
        //   0 for the one-chunk case: that chunk is a large transfer worth overlapping with
        // real work, so the poll should yield rather than spin on it. A row-chunked region is
        // the other way round -- a freshly kicked row is essentially never complete on the
        // next check, and yielding on that would advance one row per scheduler tick
        return _region_is_full_width(dev) ? 0 : SSD1351_ROW_CHUNKS_PER_POLL;
}
static void _ssd1351_async_kick(struct display_device *dev, uint16_t chunk_index)
{
        struct ssd1351_state *state = (struct ssd1351_state *) dev->driver_ctx->state;
        const struct display_region *r = &dev->update_region;
        uint16_t px = _px_bytes(dev);

        // window armed once, on the first chunk: RAM-write data wraps from x1 back to x0 at
        // each row by itself, so later rows are just more data with no re-addressing
        if(chunk_index == 0) {
                light_display_ssd1351_command_set_window(dev, r->x0, r->y0, r->x1, r->y1);
                light_display_ssd1351_command_ram_write(dev);
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
static bool _ssd1351_async_chunk_complete(struct display_device *dev)
{
        struct ssd1351_state *state = (struct ssd1351_state *) dev->driver_ctx->state;
        return light_ioport_burst_is_complete(state->io_ctx);
}

struct display_device *light_display_ssd1351_create_device(uint8_t *name, uint16_t width, uint16_t height, struct io_context *io)
{
        // io_ctx must be attached before the device is registered: adding it to the object
        // tree triggers init_device()/reset(), which read state->io_ctx -- the same ordering
        // requirement light_display_st7735_create_device() has
        struct display_device *dev = light_object_alloc(sizeof(struct display_device));
        struct display_driver_context *driver_ctx = _ssd1351_spawn_context();
        struct ssd1351_state *state = (struct ssd1351_state *) driver_ctx->state;
        state->io_ctx = io;

        return light_display_init_device(dev, driver_ctx, width, height, 16, "%s", name);
}

void light_display_ssd1351_set_master_contrast(struct display_device *dev, uint8_t level)
{
        if(level > SSD1351_MASTER_CONTRAST_MAX)
                level = SSD1351_MASTER_CONTRAST_MAX;
        _cmd(dev, SSD1351_CMD_CONTRASTMASTER, &level, 1);
}
void light_display_ssd1351_set_contrast(struct display_device *dev, uint8_t a, uint8_t b, uint8_t c)
{
        _cmd(dev, SSD1351_CMD_CONTRASTABC, (const uint8_t[]){ a, b, c }, 3);
}
void light_display_ssd1351_command_set_display_on(struct display_device *dev, bool enable)
{
        struct ssd1351_state *state = (struct ssd1351_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx,
                        enable ? SSD1351_CMD_DISPLAYON : SSD1351_CMD_DISPLAYOFF);
}
void light_display_ssd1351_command_set_inversion(struct display_device *dev, bool enable)
{
        struct ssd1351_state *state = (struct ssd1351_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx,
                        enable ? SSD1351_CMD_INVERTDISPLAY : SSD1351_CMD_NORMALDISPLAY);
}
void light_display_ssd1351_command_set_window(struct display_device *dev,
        uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
        //   ONE BYTE PER EDGE, not the two ST77xx takes. This controller addresses a 128x128
        // GDDRAM, so a column or row fits in a byte, and the window command is two parameters
        // rather than four. Copying ST7735's set_window and only changing the opcodes is the
        // obvious mistake here: the extra bytes would be read as the next command
        _cmd(dev, SSD1351_CMD_SETCOLUMN, (const uint8_t[]){ (uint8_t)x0, (uint8_t)x1 }, 2);
        _cmd(dev, SSD1351_CMD_SETROW, (const uint8_t[]){ (uint8_t)y0, (uint8_t)y1 }, 2);
}
void light_display_ssd1351_command_ram_write(struct display_device *dev)
{
        struct ssd1351_state *state = (struct ssd1351_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SSD1351_CMD_WRITERAM);
}
