#include <light_display_st7735.h>
#include <light_platform.h>

// covers this chip family's largest native panel width (132), not just this board's 160-in-
// landscape -- the buffer is indexed by dev->width, so it must bound whichever is larger
#define ST7735_MAX_CLEAR_ROW_PIXELS     162

//   upper bound before an update is considered stuck. A full 160x80x2 = 25600 byte frame at
// the 8MHz this board's SPI runs is ~26ms, so 500ms is ample headroom and matches the ST7789
// driver's figure rather than inventing a second number for the same purpose
#define ST7735_ASYNC_TIMEOUT_MS         500
#define ST7735_ROW_CHUNKS_PER_POLL      8

struct st7735_state {
        struct io_context *io_ctx;
        uint16_t col_offset;
        uint16_t row_offset;
};

static struct display_driver_context *_st7735_spawn_context();
static void _st7735_init(struct display_device *dev);
static void _st7735_reset(struct display_device *dev);
static void _st7735_clear(struct display_device *dev, uint16_t value);
static uint16_t _st7735_async_chunk_count(struct display_device *dev);
static uint16_t _st7735_async_chunks_per_poll(struct display_device *dev);
static void _st7735_async_kick(struct display_device *dev, uint16_t chunk_index);
static bool _st7735_async_chunk_complete(struct display_device *dev);

static struct display_driver _driver_st7735 = {
        .name = "display.driver:st7735",
        .spawn_context = _st7735_spawn_context,
        .init_device = _st7735_init,
        .reset = _st7735_reset,
        .clear = _st7735_clear,
        .async_chunk_count = _st7735_async_chunk_count,
        .async_kick = _st7735_async_kick,
        .async_chunk_complete = _st7735_async_chunk_complete,
        .async_timeout_ms = ST7735_ASYNC_TIMEOUT_MS,
        .async_chunks_per_poll = _st7735_async_chunks_per_poll
};

struct display_driver *light_display_driver_st7735()
{
        return &_driver_st7735;
}

static struct display_driver_context *_st7735_spawn_context()
{
        struct display_driver_context *ctx = light_alloc(sizeof(struct display_driver_context));
        ctx->driver = light_display_driver_st7735();
        ctx->state = light_alloc(sizeof(struct st7735_state));
        // light_alloc() is a plain malloc(), not zeroed -- must be set explicitly
        struct st7735_state *state = (struct st7735_state *) ctx->state;
        state->col_offset = ST7735_COL_OFFSET_DEFAULT;
        state->row_offset = ST7735_ROW_OFFSET_DEFAULT;
        return ctx;
}

static void _st7735_init(struct display_device *dev)
{
        light_display_st7735_chip_setup(dev);
}
static void _st7735_reset(struct display_device *dev)
{
        light_display_st7735_reset_device(dev);
}
static void _st7735_clear(struct display_device *dev, uint16_t value)
{
        light_display_st7735_clear_screen(dev, value);
}

void light_display_st7735_set_offset(struct display_device *dev, uint16_t col_offset, uint16_t row_offset)
{
        struct st7735_state *state = (struct st7735_state *) dev->driver_ctx->state;
        state->col_offset = col_offset;
        state->row_offset = row_offset;
}

void light_display_st7735_reset_device(struct display_device *dev)
{
        struct st7735_state *state = (struct st7735_state *) dev->driver_ctx->state;
        //   on the MiniSTM32H7xx this does nothing and says so: the panel's reset is not on a
        // GPIO (LIGHT_IOPORT_PIN_NONE), so light_ioport skips the pulse. The SWRESET below is
        // what actually resets the controller there, which is why chip_setup() issues both
        // rather than treating the hardware pulse as sufficient
        light_ioport_signal_reset(state->io_ctx);
}

//   a command byte plus its parameters, which is most of an ST7735 init. Written as a helper
// because the sequence below is long enough that spelling out send_command/send_data at each
// step buries the actual values, which are the part worth reading
static void _cmd(struct display_device *dev, uint8_t cmd, const uint8_t *args, uint8_t len)
{
        struct st7735_state *state = (struct st7735_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, cmd);
        if(len)
                light_ioport_send_data_burst(state->io_ctx, args, len);
}

void light_display_st7735_chip_setup(struct display_device *dev)
{
        light_display_st7735_reset_device(dev);
        light_display_st7735_command_sw_reset(dev);
        light_platform_sleep_ms(150);          // datasheet: >=120ms after SWRESET
        light_display_st7735_command_sleep_out(dev);
        light_platform_sleep_ms(120);          // datasheet: >=120ms after SLPOUT

        //   FRAME RATE. Three sets, for normal/idle/partial mode: (RTNA, front porch, back
        // porch), giving roughly 60Hz off the internal oscillator. Unlike ST7789 this part
        // will not produce a usable image on its defaults, so these are not optional tuning
        _cmd(dev, ST7735_CMD_FRMCTR1, (const uint8_t[]){ 0x01, 0x2C, 0x2D }, 3);
        _cmd(dev, ST7735_CMD_FRMCTR2, (const uint8_t[]){ 0x01, 0x2C, 0x2D }, 3);
        // partial mode takes six: three for each of the two halves of the frame
        _cmd(dev, ST7735_CMD_FRMCTR3, (const uint8_t[]){ 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D }, 6);
        // column inversion, which is what this panel's glass expects
        _cmd(dev, ST7735_CMD_INVCTR, (const uint8_t[]){ 0x07 }, 1);

        // POWER. AVDD/VRHP/VRHN, then the boost and op-amp settings. These are the values
        // Sitronix give for a 3.3V panel of this size and the ones WeAct's own driver uses
        _cmd(dev, ST7735_CMD_PWCTR1, (const uint8_t[]){ 0xA2, 0x02, 0x84 }, 3);
        _cmd(dev, ST7735_CMD_PWCTR2, (const uint8_t[]){ 0xC5 }, 1);
        _cmd(dev, ST7735_CMD_PWCTR3, (const uint8_t[]){ 0x0A, 0x00 }, 2);
        _cmd(dev, ST7735_CMD_PWCTR4, (const uint8_t[]){ 0x8A, 0x2A }, 2);
        _cmd(dev, ST7735_CMD_PWCTR5, (const uint8_t[]){ 0x8A, 0xEE }, 2);
        _cmd(dev, ST7735_CMD_VMCTR1, (const uint8_t[]){ 0x0E }, 1);

        //   GAMMA. Sixteen entries each for the positive and negative curves. Omitting these
        // leaves the panel washed out rather than broken, so it looks like a contrast problem
        // rather than a missing init step
        _cmd(dev, ST7735_CMD_GMCTRP1, (const uint8_t[]){
                0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d,
                0x29, 0x25, 0x2b, 0x39, 0x00, 0x01, 0x03, 0x10 }, 16);
        _cmd(dev, ST7735_CMD_GMCTRN1, (const uint8_t[]){
                0x03, 0x1d, 0x07, 0x06, 0x2e, 0x2c, 0x29, 0x2d,
                0x2e, 0x2e, 0x37, 0x3f, 0x00, 0x00, 0x02, 0x10 }, 16);

        light_display_st7735_command_set_colmod(dev, ST7735_COLMOD_16BPP);
        light_display_st7735_command_set_madctl(dev, ST7735_MADCTL_LANDSCAPE_ROT180);
        //   inversion ON. These small IPS panels are normally-black parts whose polarity is
        // the opposite of the controller's default, exactly as this project's ST7789 board
        // turned out to be -- with it off the background renders white instead of black
        light_display_st7735_command_set_inversion(dev, true);

        light_ioport_send_command_byte(((struct st7735_state *)dev->driver_ctx->state)->io_ctx,
                        ST7735_CMD_NORON);
        light_platform_sleep_ms(10);
        light_display_st7735_command_set_display_on(dev, true);
        light_platform_sleep_ms(20);
}

void light_display_st7735_clear_screen(struct display_device *dev, uint16_t color)
{
        struct st7735_state *state = (struct st7735_state *) dev->driver_ctx->state;
        light_display_st7735_command_set_window(dev, 0, 0, dev->width - 1, dev->height - 1);
        light_display_st7735_command_ram_write(dev);

        uint8_t row_buf[ST7735_MAX_CLEAR_ROW_PIXELS * 2];
        uint16_t n = dev->width < ST7735_MAX_CLEAR_ROW_PIXELS ? dev->width : ST7735_MAX_CLEAR_ROW_PIXELS;
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

// rend's 16bpp buffer is row-major RGB565 big-endian, matching RAMWR's streaming order
// exactly, so a run of pixels goes to the transport straight from the render buffer -- but
// only where that run is contiguous, which a region's rows are only at full panel width
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
static uint16_t _st7735_async_chunk_count(struct display_device *dev)
{
        return _region_is_full_width(dev) ? 1 : _region_rows(dev);
}
static uint16_t _st7735_async_chunks_per_poll(struct display_device *dev)
{
        return _region_is_full_width(dev) ? 0 : ST7735_ROW_CHUNKS_PER_POLL;
}
static void _st7735_async_kick(struct display_device *dev, uint16_t chunk_index)
{
        struct st7735_state *state = (struct st7735_state *) dev->driver_ctx->state;
        const struct display_region *r = &dev->update_region;
        uint16_t px = _px_bytes(dev);

        // window armed once, on the first chunk: RAMWR data wraps from x1 back to x0 at each
        // row by itself, so later rows are just more data with no re-addressing
        if(chunk_index == 0) {
                light_display_st7735_command_set_window(dev, r->x0, r->y0, r->x1, r->y1);
                light_display_st7735_command_ram_write(dev);
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
static bool _st7735_async_chunk_complete(struct display_device *dev)
{
        struct st7735_state *state = (struct st7735_state *) dev->driver_ctx->state;
        return light_ioport_burst_is_complete(state->io_ctx);
}

struct display_device *light_display_st7735_create_device(uint8_t *name, uint16_t width, uint16_t height, struct io_context *io)
{
        // io_ctx must be attached before the device is registered: adding it to the object
        // tree triggers init_device()/reset(), which read state->io_ctx -- the same ordering
        // requirement as light_display_st7789_create_device()
        struct display_device *dev = light_object_alloc(sizeof(struct display_device));
        struct display_driver_context *driver_ctx = _st7735_spawn_context();
        struct st7735_state *state = (struct st7735_state *) driver_ctx->state;
        state->io_ctx = io;

        return light_display_init_device(dev, driver_ctx, width, height, 16, "%s", name);
}

void light_display_st7735_command_sw_reset(struct display_device *dev)
{
        struct st7735_state *state = (struct st7735_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, ST7735_CMD_SWRESET);
}
void light_display_st7735_command_sleep_out(struct display_device *dev)
{
        struct st7735_state *state = (struct st7735_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, ST7735_CMD_SLPOUT);
}
void light_display_st7735_command_set_colmod(struct display_device *dev, uint8_t format)
{
        _cmd(dev, ST7735_CMD_COLMOD, &format, 1);
}
void light_display_st7735_command_set_madctl(struct display_device *dev, uint8_t bits)
{
        _cmd(dev, ST7735_CMD_MADCTL, &bits, 1);
}
void light_display_st7735_command_set_inversion(struct display_device *dev, bool enable)
{
        struct st7735_state *state = (struct st7735_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, enable ? ST7735_CMD_INVON : ST7735_CMD_INVOFF);
}
void light_display_st7735_command_set_display_on(struct display_device *dev, bool enable)
{
        struct st7735_state *state = (struct st7735_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, enable ? ST7735_CMD_DISPON : ST7735_CMD_DISPOFF);
}
void light_display_st7735_command_set_window(struct display_device *dev,
        uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
        struct st7735_state *state = (struct st7735_state *) dev->driver_ctx->state;
        uint16_t cx0 = x0 + state->col_offset;
        uint16_t cx1 = x1 + state->col_offset;
        uint16_t cy0 = y0 + state->row_offset;
        uint16_t cy1 = y1 + state->row_offset;

        light_ioport_send_command_byte(state->io_ctx, ST7735_CMD_CASET);
        uint8_t caset[4] = { (uint8_t)(cx0 >> 8), (uint8_t)(cx0 & 0xFF), (uint8_t)(cx1 >> 8), (uint8_t)(cx1 & 0xFF) };
        light_ioport_send_data_burst(state->io_ctx, caset, 4);

        light_ioport_send_command_byte(state->io_ctx, ST7735_CMD_RASET);
        uint8_t raset[4] = { (uint8_t)(cy0 >> 8), (uint8_t)(cy0 & 0xFF), (uint8_t)(cy1 >> 8), (uint8_t)(cy1 & 0xFF) };
        light_ioport_send_data_burst(state->io_ctx, raset, 4);
}
void light_display_st7735_command_ram_write(struct display_device *dev)
{
        struct st7735_state *state = (struct st7735_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, ST7735_CMD_RAMWR);
}
