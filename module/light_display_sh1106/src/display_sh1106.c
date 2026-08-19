#include <light_display_sh1106.h>

#include "light_display_sh1106_internal.h"

// SH1106 hardware constants: 132 GDDRAM columns (segments) x 8 pages (64 rows) -- fixed by the
// silicon, not something this driver's addressing scheme configures. used to size the
// per-page burst buffer and to bounds-check y_to_pages()/reset_device()
#define SH1106_MAX_COLUMNS 132
#define SH1106_MAX_PAGES   8

// upper bound on one whole update before it's treated as stuck. bus-bound: 8 pages of up
// to 132 bytes is a little over 1KB including command overhead, well under 20ms even at a
// conservatively slow clock -- 50ms leaves ample headroom
#define SH1106_ASYNC_TIMEOUT_MS 50
// a chunk here is one page: an up-to-132-byte burst, of which there are at most 8. small
// enough that yielding after every one would waste a scheduler tick apiece
#define SH1106_CHUNKS_PER_POLL  4

struct sh1106_state {
        struct io_context *io_ctx;
        uint8_t page_address;
        uint8_t column_address;
        uint16_t n_pages;
        uint16_t n_columns;
        // driver-level preferences, not chip state -- set once via
        // light_display_sh1106_set_sweep_direction()/set_column_offset(), survive resets
        uint8_t sweep_direction;
        uint8_t column_offset;
        // not a stack local like the blocking path's used to be: an async burst is read by
        // DMA after the function that filled it returns, so it has to survive at least
        // until the transport reports the burst complete
        uint8_t update_col_buf[SH1106_MAX_COLUMNS];
};

static struct display_driver_context *_sh1106_spawn_context();
static void _sh1106_destroy_context(struct display_driver_context *ctx);
static void _sh1106_init(struct display_device *dev);
static void _sh1106_reset(struct display_device *dev);
static void _sh1106_clear(struct display_device *dev, uint16_t value);
static uint16_t _sh1106_async_chunk_count(struct display_device *dev);
static uint16_t _sh1106_async_chunks_per_poll(struct display_device *dev);
static void _sh1106_async_kick(struct display_device *dev, uint16_t chunk_index);
static bool _sh1106_async_chunk_complete(struct display_device *dev);

// one chunk is one page: SH1106 auto-increments the COLUMN address within a page, the
// opposite axis to SH1107, so a page is this chip's natural single-burst unit
static struct display_driver _driver_sh1106 = {
        .name = "display.driver:sh1106",
        .spawn_context = _sh1106_spawn_context,
        .destroy_context = _sh1106_destroy_context,
        .init_device = _sh1106_init,
        .reset = _sh1106_reset,
        .clear = _sh1106_clear,
        .async_chunk_count = _sh1106_async_chunk_count,
        .async_kick = _sh1106_async_kick,
        .async_chunk_complete = _sh1106_async_chunk_complete,
        .async_timeout_ms = SH1106_ASYNC_TIMEOUT_MS,
        .async_chunks_per_poll = _sh1106_async_chunks_per_poll
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
        // light_alloc() is a plain malloc(), not zeroed -- these must be set explicitly
        // rather than relying on them happening to start at their intended defaults
        struct sh1106_state *state = (struct sh1106_state *) ctx->state;
        state->sweep_direction = SH1106_SWEEP_FORWARD;
        state->column_offset = SH1106_COLUMN_OFFSET_DEFAULT;
        return ctx;
}

//   the counterpart to _sh1106_spawn_context(), called from the device release path
// when the device this context was spawned for is freed. Frees in the reverse of
// the order allocated: the state first, then the context that points at it
static void _sh1106_destroy_context(struct display_driver_context *ctx)
{
        light_free((void *)ctx->state);
        light_free(ctx);
}

static void _sh1106_init(struct display_device *dev)
{
        light_display_sh1106_chip_setup(dev);
}
static void _sh1106_reset(struct display_device *dev)
{
        light_display_sh1106_reset_device(dev);
}
static void _sh1106_clear(struct display_device *dev, uint16_t value)
{
        // 1bpp: only the low byte is meaningful (0/1-per-bit paradigm), matching this
        // driver's pre-existing behavior -- the vtable's clear slot itself was widened for
        // 16bpp color drivers, this one just doesn't need the extra range
        light_display_sh1106_clear_screen(dev, (uint8_t)value);
}
// physical page = group of 8 canvas y rows (see reset_device()), so the update region's y
// extent maps onto a page range, rounded outward to whole pages. clamped to the panel,
// since the region comes from caller-supplied logical coordinates. the region's x extent
// is deliberately ignored: every page burst covers the full column width, so this driver's
// region granularity is page-only
static uint16_t _sweep_page_begin(struct display_device *dev)
{
        return dev->update_region.y0 / 8;
}
static uint16_t _sweep_count(struct display_device *dev)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        uint16_t limit = state->n_pages < SH1106_MAX_PAGES ? state->n_pages : SH1106_MAX_PAGES;
        uint16_t begin = _sweep_page_begin(dev);
        if(begin >= limit)
                return 0;
        uint16_t last = dev->update_region.y1 / 8;
        if(last >= limit)
                last = limit - 1;
        return last - begin + 1;
}
// maps sweep index i (0 .. count-1) to the hardware page to visit at that step, per
// state->sweep_direction. page (not column) is the dimension swept here because it's the
// column address that auto-increments within a page on real SH1106 hardware -- the opposite
// of SH1107, where the page address auto-increments within a column. the reverse branch
// anchors to the end of the REGION, not the end of the panel
static uint16_t _sweep_page(struct display_device *dev, uint16_t i)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        if(state->sweep_direction == SH1106_SWEEP_REVERSE)
                return _sweep_page_begin(dev) + _sweep_count(dev) - 1 - i;
        return _sweep_page_begin(dev) + i;
}
void light_display_sh1106_set_sweep_direction(struct display_device *dev, uint8_t direction)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        state->sweep_direction = direction;
}
void light_display_sh1106_set_column_offset(struct display_device *dev, uint8_t offset)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        state->column_offset = offset;
}

void light_display_sh1106_reset_device(struct display_device *dev)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_signal_reset(state->io_ctx);
        // straightforward, non-rotated mapping: physical column = canvas x + column_offset,
        // physical page = group of 8 canvas y rows. dev->width/height are expected to already
        // describe the panel's real physical orientation
        state->n_pages = light_display_sh1106_y_to_pages(dev->height);
        state->n_columns = light_display_sh1106_x_to_columns(dev->width);
        state->column_address = 0xFF;
        state->page_address = 0xFF;
}
void light_display_sh1106_chip_setup(struct display_device *dev)
{
        light_display_sh1106_reset_device(dev);
        light_display_sh1106_command_set_display_on(dev, false);            // display OFF
        light_display_sh1106_command_set_display_offset(dev, 0x00);         // no display offset
        light_display_sh1106_command_set_start_line(dev, 0x00);             // start line 0
        // "VCC generated by internal DC-DC circuit" variant of the reference driver's
        // application example -- unlike this project's SH1107 driver (which assumes a
        // separately-supplied high-voltage VCC rail and so leaves DC-DC disabled), this
        // panel only exposes a single power input, so the chip's own charge pump has to be
        // switched on to generate the OLED's actual drive voltage at all. with DC-DC left
        // off, the logic side (VDD) still powers up fine -- explaining a confirmed voltage
        // reading on the power pins -- but the panel itself never lights, since it never
        // receives a real VCC
        light_display_sh1106_command_set_dcdc(dev, true);                   // 0xAD, 0x8B
        light_display_sh1106_command_set_vpp(dev, 0x03);                    // 0x33 (9V)
        light_display_sh1106_command_set_segment_remap(dev, true);          // 0xA1
        light_display_sh1106_command_set_reverse_display(dev, false);       // 0xA6 (normal)
        light_display_sh1106_command_set_scan_dir(dev, SH1106_SCAN_DIR_REMAPPED); // 0xC8
        light_display_sh1106_command_set_com_pins(dev, true, false);        // 0xDA, 0x12
        light_display_sh1106_command_set_contrast(dev, 0xFF);               // 0x81, 0xFF
        light_display_sh1106_command_set_charge_periods(dev, 0xF, 1);       // 0xD9, 0x1F
        light_display_sh1106_command_set_multiplex_ratio(dev, 0x3F);        // 1:64 duty
        light_display_sh1106_command_set_vcom_deselect(dev, 0x40);          // 0xDB, 0x40
        light_display_sh1106_command_set_display_clock(dev, 8, 0);          // 0xD5, 0x80
        light_display_sh1106_command_set_force_on(dev, false);              // obey RAM contents
        light_display_sh1106_command_set_display_on(dev, true);             // display ON
}
void light_display_sh1106_clear_screen(struct display_device *dev, uint8_t value)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        uint8_t col_buf[SH1106_MAX_COLUMNS];
        uint16_t n = state->n_columns < SH1106_MAX_COLUMNS ? state->n_columns : SH1106_MAX_COLUMNS;
        for(uint16_t i = 0; i < n; i++)
                col_buf[i] = value;

        // deliberately NOT _sweep_page(): that maps an index within the current update
        // REGION, whereas a clear always covers the whole panel. sweep direction only
        // affects the order bytes are emitted, never where they land, and every page here
        // gets the same uniform fill -- so a plain ascending walk is equivalent
        for(uint16_t i = 0; i < state->n_pages && i < SH1106_MAX_PAGES; i++) {
                uint16_t page = i;
                light_display_sh1106_command_set_page_addr(dev, page);
                // invalidate the tracked column first: the burst below advances the chip's own
                // internal column counter well past what our software-tracked column_address
                // would show, so the "skip if unchanged" optimisation in
                // command_set_column_addr() would otherwise wrongly skip re-arming it for the
                // next page
                state->column_address = 0xFF;
                light_display_sh1106_command_set_column_addr(dev, state->column_offset);
                light_ioport_send_data_burst(state->io_ctx, col_buf, n);
        }
}
static uint16_t _sh1106_async_chunk_count(struct display_device *dev)
{
        return _sweep_count(dev);
}
// always a page, however the region was shaped, so the answer doesn't vary per update
static uint16_t _sh1106_async_chunks_per_poll(struct display_device *dev)
{
        return SH1106_CHUNKS_PER_POLL;
}
// assembles one page's worth of column bytes into state->update_col_buf, then kicks off a
// non-blocking burst send for it.
//
// light_draw's buffer is row-major with horizontal bit-packing (each byte holds 8 pixels from
// one row: buffer[y * width_bytes + x/8], bit x%8 set for the LEFTMOST pixel of that
// group), but SH1106 RAM in page addressing mode is column/page addressed, where each byte
// written covers 8 *vertically* stacked pixels within a single 1-pixel-wide column --
// these are different bit orderings/axes entirely, so each output byte is assembled
// bit-by-bit from the source buffer rather than copied across directly
static void _sh1106_async_kick(struct display_device *dev, uint16_t chunk_index)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        uint16_t width_bytes = (dev->width + 7) / 8;
        const uint8_t *src = dev->update_source_buffer;
        uint16_t n = state->n_columns < SH1106_MAX_COLUMNS ? state->n_columns : SH1106_MAX_COLUMNS;
        uint16_t page = _sweep_page(dev, chunk_index);

        light_display_sh1106_command_set_page_addr(dev, page);
        // see clear_screen() for why this must be invalidated before every page's burst
        state->column_address = 0xFF;
        light_display_sh1106_command_set_column_addr(dev, state->column_offset);

        for(uint16_t column = 0; column < n; column++) {
                uint8_t out = 0;
                for(uint8_t bit = 0; bit < 8; bit++) {
                        uint16_t y = page * 8 + bit;
                        if(y >= dev->height)
                                break;
                        uint8_t src_byte = src[y * width_bytes + column / 8];
                        if(src_byte & (1 << (column % 8)))
                                out |= (1 << bit);
                }
                state->update_col_buf[column] = out;
        }
        // one continuous burst per page: the chip auto-increments its own internal column
        // pointer between bytes within a single CS-low transfer in page addressing mode --
        // this is the opposite axis to SH1107 (which auto-increments page within a column),
        // which is why a chunk here is a page rather than a column
        light_ioport_send_data_burst_async(state->io_ctx, state->update_col_buf, n);
}
static bool _sh1106_async_chunk_complete(struct display_device *dev)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        return light_ioport_burst_is_complete(state->io_ctx);
}
struct display_device *light_display_sh1106_create_device(uint8_t *name, uint16_t width, uint16_t height, uint8_t bpp, struct io_context *io)
{
        // TODO validate *io
        // io_ctx must be attached to the driver state before the device is registered: adding
        // it to the object tree (via light_display_init_device()) immediately triggers
        // init_device()/reset(), which read state->io_ctx -- light_display_create_device() does
        // both steps in one call, too late to set io_ctx first, so the lower-level entry point
        // is used here instead
        struct display_device *dev = light_object_alloc(sizeof(struct display_device));
        struct display_driver_context *driver_ctx = _sh1106_spawn_context();
        struct sh1106_state *state = (struct sh1106_state *) driver_ctx->state;
        state->io_ctx = io;

        return light_display_init_device(dev, driver_ctx, width, height, bpp, "%s", name);
}

void light_display_sh1106_command_set_column_addr(struct display_device *dev, uint8_t column)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        // low nibble must be sent before high -- confirmed against the reference driver's own
        // application example ("set lower column address" precedes "set higher column
        // address"), and matches the SH1107 driver's own hard-won fix for the same ordering
        // requirement
        if((state->column_address & 0x0F) != (column & 0x0F))
                light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_COL_ADDR_LOW + (column & 0x0F));
        if((state->column_address & 0xF0) != (column & 0xF0))
                light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_COL_ADDR_HIGH + (column >> 4));
        state->column_address = column;
}
void light_display_sh1106_command_set_contrast(struct display_device *dev, uint8_t level)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_CONTRAST);
        light_ioport_send_command_byte(state->io_ctx, level);
}
void light_display_sh1106_command_set_segment_remap(struct display_device *dev, bool enable)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_SEG_REMAP + enable);
}
void light_display_sh1106_command_set_multiplex_ratio(struct display_device *dev, uint8_t ratio)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_MUX_RATIO);
        light_ioport_send_command_byte(state->io_ctx, ratio);
}
void light_display_sh1106_command_set_force_on(struct display_device *dev, bool enable)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_FORCE_ON + enable);
}
void light_display_sh1106_command_set_reverse_display(struct display_device *dev, bool enable)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_REVERSE + enable);
}
void light_display_sh1106_command_set_display_offset(struct display_device *dev, uint8_t data)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_DISPLAY_OFFSET);
        light_ioport_send_command_byte(state->io_ctx, data);
}
void light_display_sh1106_command_set_dcdc(struct display_device *dev, bool enable)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_DCDC);
        light_ioport_send_command_byte(state->io_ctx, enable ? 0x8B : 0x8A);
}
void light_display_sh1106_command_set_vpp(struct display_device *dev, uint8_t level)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_VPP + (level & 0x03));
}
void light_display_sh1106_command_set_display_on(struct display_device *dev, bool enable)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_DISPLAY_ON + enable);
}
void light_display_sh1106_command_set_page_addr(struct display_device *dev, uint8_t page)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        if(state->page_address != page)
                light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_PAGE_ADDR + page);
        state->page_address = page;
}
void light_display_sh1106_command_set_scan_dir(struct display_device *dev, uint8_t data)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_SCAN_DIR + data);
}
void light_display_sh1106_command_set_display_clock(struct display_device *dev, uint8_t freq, uint8_t div)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_DISPLAY_CLK);
        light_ioport_send_command_byte(state->io_ctx, ((freq & 0x0F) << 4) + (div & 0x0F));
}
void light_display_sh1106_command_set_charge_periods(struct display_device *dev, uint8_t pre, uint8_t dis)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_CHARGE_PERIODS);
        light_ioport_send_command_byte(state->io_ctx, (pre & 0x0F) + ((dis & 0x0F) << 4));
}
void light_display_sh1106_command_set_vcom_deselect(struct display_device *dev, uint8_t data)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_VCOMH);
        light_ioport_send_command_byte(state->io_ctx, data);
}
void light_display_sh1106_command_set_com_pins(struct display_device *dev, bool alternative, bool remap)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_COM_PINS);
        light_ioport_send_command_byte(state->io_ctx, 0x02 | (alternative << 4) | (remap << 5));
}
void light_display_sh1106_command_set_start_line(struct display_device *dev, uint8_t line)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        // single-byte command on SH1106 -- see SH1106_CMD_SET_START_LINE
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_SET_START_LINE + (line & 0x3F));
}
void light_display_sh1106_command_rmw_begin(struct display_device *dev)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_RMW_BEGIN);
}
void light_display_sh1106_command_rmw_end(struct display_device *dev)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_RMW_END);
}
void light_display_sh1106_command_no_op(struct display_device *dev)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_command_byte(state->io_ctx, SH1106_CMD_NOP);
}
void light_display_sh1106_write_data(struct display_device *dev, uint8_t data)
{
        struct sh1106_state *state = (struct sh1106_state *) dev->driver_ctx->state;
        light_ioport_send_data_byte(state->io_ctx, data);

        // increment address counter, to track state of driver's internal counter -- SH1106
        // only has page addressing mode, so it's always the column that auto-increments
        // (wrapping within the current page), never the page. the visible window starts at
        // column_offset (not 0), so the wrap has to be computed relative to that, not just
        // taken mod n_columns directly
        state->column_address = state->column_offset +
                ((state->column_address - state->column_offset + 1) % state->n_columns);
}
uint16_t light_display_sh1106_y_to_pages(uint16_t y)
{
        return (y / 8) + ((y % 8) ? 1 : 0);
}
uint16_t light_display_sh1106_x_to_columns(uint16_t x)
{
        return x;
}
