#include <light_display_sh1107.h>

#include "light_display_sh1107_internal.h"

// SH1107's GDDRAM depth is a fixed hardware constant (16 pages), not something this
// driver's addressing scheme configures -- used to size the per-column burst buffer
#define SH1107_MAX_PAGES 16
// upper bound on how long one FULL async update (all columns) should ever take -- if it
// hasn't completed within this window, something is genuinely stuck (miswired DMA channel,
// hardware fault) and the scheduler must not be allowed to wait on it forever.
// NOT just "one column's burst time": update_async_poll()'s drain loop only advances past a
// column once burst_is_complete() sees it finished, and it checks that once per scheduler
// tick (LIGHT_TASK_POLL_INTERVAL_MS, 1ms) -- a column's burst (tens to low hundreds of
// microseconds) almost always finishes well within that tick, so in practice this ends up
// pacing at roughly one column per tick regardless of actual bus speed, not one burst's
// worth of time for the whole sweep. worst case is therefore closer to
// (n_columns * tick interval) than to a single burst's duration -- for a 128-column-wide
// panel (SH1107's max) that's ~128ms, so this needs real headroom above that, not a value
// sized for one burst
#define SH1107_ASYNC_TIMEOUT_MS 500

struct sh1107_state {
        struct io_context *io_ctx;
        uint8_t addrmode;
        uint8_t page_address;
        uint8_t column_address;
        uint16_t n_pages;
        uint16_t n_columns;
        // driver-level preference, not chip state -- set once via
        // light_display_sh1107_set_sweep_direction(), survives resets
        uint8_t sweep_direction;
        // async update state -- see _sh1107_update_async_start()/_poll() below. sequenced
        // by column (not page), since SH1107's vertical addressing mode auto-increments the
        // page address within a column, making column the natural per-burst unit
        bool update_in_progress;
        uint16_t update_column_index;
        uint32_t update_start_time_ms;
        // moved off the stack from what used to be update_screen()'s local page_buf: an
        // async burst is read by DMA after the function that filled it returns, so it can't
        // be stack memory -- it has to survive at least until burst_is_complete()
        uint8_t update_page_buf[SH1107_MAX_PAGES];
};

static struct display_driver_context *_sh1107_spawn_context();
static void _sh1107_init(struct display_device *dev);
static void _sh1107_reset(struct display_device *dev);
static void _sh1107_clear(struct display_device *dev, uint8_t value);
static void _sh1107_update(struct display_device *dev);
static void _sh1107_update_async_start(struct display_device *dev);
static bool _sh1107_update_async_poll(struct display_device *dev);
static bool _sh1107_update_async_is_active(struct display_device *dev);

static struct display_driver _driver_sh1107 = {
        .name = "display.driver:sh1107",
        .spawn_context = _sh1107_spawn_context,
        .init_device = _sh1107_init,
        .reset = _sh1107_reset,
        .clear = _sh1107_clear,
        .update = _sh1107_update,
        .update_async_start = _sh1107_update_async_start,
        .update_async_poll = _sh1107_update_async_poll,
        .update_async_is_active = _sh1107_update_async_is_active
};

struct display_driver *light_display_driver_sh1107()
{
        return &_driver_sh1107;
}

static struct display_driver_context *_sh1107_spawn_context()
{
        struct display_driver_context *ctx = light_alloc(sizeof(struct display_driver_context));
        ctx->driver = light_display_driver_sh1107();
        ctx->state = light_alloc(sizeof(struct sh1107_state));
        // light_alloc() is a plain malloc(), not zeroed -- sweep_direction and
        // update_in_progress must be set explicitly rather than relying on them happening
        // to start at SH1107_SWEEP_FORWARD (0) / false
        struct sh1107_state *state = (struct sh1107_state *) ctx->state;
        state->sweep_direction = SH1107_SWEEP_FORWARD;
        state->update_in_progress = false;
        return ctx;
}

static void _sh1107_init(struct display_device *dev)
{
        // ASSERT dev->driver_ctx->driver == light_display_driver_sh1007()
        light_display_sh1107_chip_setup(dev);
}
static void _sh1107_reset(struct display_device *dev)
{
        // ASSERT dev->driver_ctx->driver == light_display_driver_sh1007()
        light_display_sh1107_reset_device(dev);
}
static void _sh1107_clear(struct display_device *dev, uint8_t value)
{
        light_display_sh1107_clear_screen(dev, value);
}
static void _sh1107_update(struct display_device *dev)
{
        light_display_sh1107_update_screen(dev);
}
// maps sweep index i (0 .. n_columns-1) to the actual hardware column to visit at that
// step, per state->sweep_direction
static uint16_t _sweep_column(struct sh1107_state *state, uint16_t i)
{
        if(state->sweep_direction == SH1107_SWEEP_REVERSE)
                return state->n_columns - 1 - i;
        return i;
}
void light_display_sh1107_set_sweep_direction(struct display_device *dev, uint8_t direction)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        state->sweep_direction = direction;
}
// unconditionally sends both column-address nibble commands, low nibble first --
// matches the reference driver's show() exactly, which always sends both regardless of
// the previous column, rather than trusting a "skip if unchanged" optimization
static void _send_column_addr_unconditional(struct display_device *dev, uint8_t column)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_COL_ADDR_LOW + (column & 0x0F));
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_COL_ADDR_HIGH + (column >> 4));
        state->column_address = column;
}

void light_display_sh1107_reset_device(struct display_device *dev)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_signal_reset(state->io_ctx);
        // straightforward, non-rotated mapping: physical column = canvas x directly,
        // physical page = group of 8 canvas y rows. dev->width/height are expected to
        // already describe the panel's real physical orientation (PO13 is 64x128
        // portrait -- see light_display_po13.h)
        state->n_pages = light_display_sh1107_y_to_pages(dev->height);
        state->n_columns = light_display_sh1107_x_to_columns(dev->width);
        state->addrmode = SH1107_ADDRMODE_VERTICAL;
        state->column_address = 0xFF;
        state->page_address = 0xFF;
}
void light_display_sh1107_chip_setup(struct display_device *dev)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_sh1107_reset_device(dev);
        light_display_sh1107_command_set_display_on(dev, false);        // display OFF
        // state->column_address/page_address are still the 0xFF sentinel reset_device()
        // just set (meaning "unknown"/"not yet sent") -- these calls must initialize to
        // an actual valid address (0), not resend that sentinel as if it were one
        light_display_sh1107_command_set_column_addr(dev, 0);           // set column address (0x0)
        light_display_sh1107_command_set_page_addr(dev, 0);             // set page address (0x0)
        light_display_sh1107_command_set_start_line(dev, 0x0);          // set display start line (0)
        light_display_sh1107_command_set_contrast(dev, 128);            // set contrast level (128)
        light_display_sh1107_command_set_addrmode(dev, state->addrmode);       // RAM addressing mode (vertical)
        light_display_sh1107_command_set_segment_remap(dev, false);     // set segment remap OFF
        light_display_sh1107_command_set_scan_dir(dev, SH1107_SCAN_DIR_DOWN);   // set common scan direction (down)
        light_display_sh1107_command_set_force_on(dev, false);          // set force all pixels (disable)
        light_display_sh1107_command_set_reverse_display(dev, false);   // set reverse mode OFF
        light_display_sh1107_command_set_multiplex_ratio(dev, 63);     // set multiplex ratio (1:64) -- fixed, this panel only has 64 COM lines
        light_display_sh1107_command_set_display_offset(dev, 96);       // set display offset (48)
        light_display_sh1107_command_set_display_clock(dev, 4, 1);      // set oscillator freq ([f-5%]/2)
        light_display_sh1107_command_set_charge_periods(dev, 2, 2);     // set pre-charge (2), dis-charge (2)
        light_display_sh1107_command_set_vcom_deselect(dev, 0x35);      // set VcomH (0.770 x Vref)
        light_display_sh1107_command_set_power_mode(dev, false, 5);     // set built-in DC-DC OFF
        light_display_sh1107_command_set_display_on(dev, true);         // display ON
}
void light_display_sh1107_clear_screen(struct display_device *dev, uint8_t value)
{
        // ASSERT dev->driver_ctx->driver === &_driver_sh1107
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        uint8_t page_buf[SH1107_MAX_PAGES];
        for(uint16_t i = 0; i < state->n_pages && i < SH1107_MAX_PAGES; i++)
                page_buf[i] = value;

        for(uint16_t i = 0; i < state->n_columns; i++) {
                uint16_t column = _sweep_column(state, i);
                _send_column_addr_unconditional(dev, column);
                // page address is (re-)armed once per column, then the whole column's
                // worth of pages goes out as a single burst -- the chip auto-increments
                // its own internal page pointer between bytes within one continuous
                // CS-low transfer in vertical addressing mode (this is exactly how the
                // reference driver for this panel does it). invalidate the tracked page
                // first: write_data()'s old per-byte bookkeeping would have wrapped
                // back to 0 by construction after n_pages writes, so set_page_addr(dev,
                // 0) would otherwise see "already at 0" and skip sending the command
                state->page_address = 0xFF;
                light_display_sh1107_command_set_page_addr(dev, 0);
                light_display_ioport_send_data_burst(state->io_ctx, page_buf, state->n_pages);
        }
}
void light_display_sh1107_update_screen(struct display_device *dev)
{
        // ASSERT dev->driver_ctx->driver === &_driver_sh1107
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;

        // straightforward, non-rotated mapping: physical column = canvas x directly.
        // rend's buffer is row-major with horizontal bit-packing (each byte holds 8
        // pixels from one row: buffer[y * width_bytes + x/8], bit x%8 set for the
        // LEFTMOST pixel of that group), but SH1107 RAM in vertical addressing mode is
        // column/page addressed, where each byte written covers 8 *vertically* stacked
        // pixels within a single 1-pixel-wide column -- these are different bit
        // orderings/axes entirely, so each output byte is assembled bit-by-bit from
        // the source buffer rather than copied across directly
        uint16_t width_bytes = (dev->width + 7) / 8;
        const uint8_t *src = dev->render_ctx->buffer;
        uint8_t page_buf[SH1107_MAX_PAGES];

        for(uint16_t i = 0; i < state->n_columns; i++) {
                uint16_t column = _sweep_column(state, i);
                _send_column_addr_unconditional(dev, column);

                for(uint16_t page = 0; page < state->n_pages && page < SH1107_MAX_PAGES; page++) {
                        uint8_t out = 0;
                        for(uint8_t bit = 0; bit < 8; bit++) {
                                uint16_t y = page * 8 + bit;
                                if(y >= dev->height)
                                        break;
                                uint8_t src_byte = src[y * width_bytes + column / 8];
                                if(src_byte & (1 << (column % 8)))
                                        out |= (1 << bit);
                        }
                        page_buf[page] = out;
                }
                // one continuous burst per column: the chip auto-increments its own
                // internal page pointer between bytes within a single CS-low transfer
                // in vertical addressing mode -- see clear_screen() for why the
                // per-byte page-address resend this replaces was there in the first
                // place, and why it's safe to drop for a burst
                state->page_address = 0xFF;
                light_display_sh1107_command_set_page_addr(dev, 0);
                light_display_ioport_send_data_burst(state->io_ctx, page_buf, state->n_pages);
        }
}
// assembles one column's worth of page bytes from the render buffer (same bit-transpose as
// light_display_sh1107_update_screen()'s loop body, just for a single column) into
// state->update_page_buf, then kicks off a non-blocking burst send for it. shared by
// _sh1107_update_async_start() (first column) and _sh1107_update_async_poll() (every column
// after)
static void _sh1107_update_kick_column(struct display_device *dev, uint16_t sweep_index)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        uint16_t width_bytes = (dev->width + 7) / 8;
        const uint8_t *src = dev->render_ctx->buffer;
        uint16_t column = _sweep_column(state, sweep_index);

        _send_column_addr_unconditional(dev, column);

        for(uint16_t page = 0; page < state->n_pages && page < SH1107_MAX_PAGES; page++) {
                uint8_t out = 0;
                for(uint8_t bit = 0; bit < 8; bit++) {
                        uint16_t y = page * 8 + bit;
                        if(y >= dev->height)
                                break;
                        uint8_t src_byte = src[y * width_bytes + column / 8];
                        if(src_byte & (1 << (column % 8)))
                                out |= (1 << bit);
                }
                state->update_page_buf[page] = out;
        }
        state->page_address = 0xFF;
        light_display_sh1107_command_set_page_addr(dev, 0);
        light_display_ioport_send_data_burst_async(state->io_ctx, state->update_page_buf, state->n_pages);
}
static void _sh1107_update_async_start(struct display_device *dev)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        if(state->update_in_progress) {
                light_warn("update already in progress for device '%s', ignoring", dev->header.id);
                return;
        }
        state->update_in_progress = true;
        state->update_column_index = 0;
        state->update_start_time_ms = light_platform_get_time_since_init();
        _sh1107_update_kick_column(dev, 0);
}
// intended to drain as many already-completed columns as possible in one call, so a fast
// bus wouldn't be artificially capped at one column per scheduler tick. in practice a single
// column's burst (tens to low hundreds of microseconds) almost always finishes well within
// one tick, so kicking off column N+1 and immediately re-checking burst_is_complete() in the
// same iteration nearly always sees "still busy" and exits the loop -- so this ends up
// pacing at roughly one column per tick regardless of bus speed. that's fine: the actual
// goal was freeing the CPU between transfers (which this does -- see SH1107_ASYNC_TIMEOUT_MS
// for why the timeout has to account for this pacing), not minimizing wall-clock time to
// finish a whole-screen update
static bool _sh1107_update_async_poll(struct display_device *dev)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        if(!state->update_in_progress)
                return true;

        if(light_platform_get_time_since_init() - state->update_start_time_ms > SH1107_ASYNC_TIMEOUT_MS) {
                light_error("async update timed out for device '%s', aborting", dev->header.id);
                state->update_in_progress = false;
                return true;
        }

        while(light_display_ioport_burst_is_complete(state->io_ctx)) {
                state->update_column_index++;
                if(state->update_column_index >= state->n_columns) {
                        state->update_in_progress = false;
                        return true;
                }
                _sh1107_update_kick_column(dev, state->update_column_index);
        }
        return false;
}
static bool _sh1107_update_async_is_active(struct display_device *dev)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        return state->update_in_progress;
}
struct display_device *light_display_sh1107_create_device(uint8_t *name, uint16_t width, uint16_t height, uint8_t bpp, struct io_context *io)
{
        // TODO validate *io
        // io_ctx must be attached to the driver state before the device is registered: adding
        // it to the object tree (via light_display_init_device()) immediately triggers
        // init_device()/reset(), which read state->io_ctx -- light_display_create_device() does
        // both steps in one call, too late to set io_ctx first, so the lower-level entry point
        // is used here instead
        struct display_device *dev = light_object_alloc(sizeof(struct display_device));
        struct display_driver_context *driver_ctx = _sh1107_spawn_context();
        struct sh1107_state *state = (struct sh1107_state *) driver_ctx->state;
        state->io_ctx = io;

        return light_display_init_device(dev, driver_ctx, width, height, bpp, "%s", name);
}

void light_display_sh1107_command_set_column_addr(struct display_device *dev, uint8_t column)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        // low nibble must be sent before high, matching Waveshare's reference driver
        // for this panel -- every other command byte/order in this file was verified
        // against that reference and matched exactly except this one
        if((state->column_address & 0x0F) != (column & 0x0F))
                light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_COL_ADDR_LOW + (column & 0x0F));
        if((state->column_address & 0xF0) != (column & 0xF0))
                light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_COL_ADDR_HIGH + (column >> 4));
        state->column_address = column;
}
void light_display_sh1107_command_set_addrmode(struct display_device *dev, uint8_t mode)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_ADDRMODE + mode);
}
void light_display_sh1107_command_set_contrast(struct display_device *dev, uint8_t level)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_CONTRAST);
        light_display_ioport_send_command_byte(state->io_ctx, level);
}
void light_display_sh1107_command_set_segment_remap(struct display_device *dev, bool enable)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_SEG_REMAP + enable);
}
extern void light_display_sh1107_command_set_multiplex_ratio(struct display_device *dev, uint8_t ratio)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_MUX_RATIO);
        light_display_ioport_send_command_byte(state->io_ctx, ratio);
}
void light_display_sh1107_command_set_force_on(struct display_device *dev, bool enable)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_FORCE_ON + enable);
}
void light_display_sh1107_command_set_reverse_display(struct display_device *dev, bool enable)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_REVERSE + enable);
}
void light_display_sh1107_command_set_display_offset(struct display_device *dev, uint8_t data)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_DISPLAY_OFFSET);
        light_display_ioport_send_command_byte(state->io_ctx, data);
}
void light_display_sh1107_command_set_power_mode(struct display_device *dev, bool enable, uint8_t mode)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_POWER_MODE);
        light_display_ioport_send_command_byte(state->io_ctx, 0x80 + enable + (mode << 1));
}
void light_display_sh1107_command_set_display_on(struct display_device *dev, bool enable)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_DISPLAY_ON + enable);
}
void light_display_sh1107_command_set_page_addr(struct display_device *dev, uint8_t page)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        if(state->page_address != page)
                light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_PAGE_ADDR + page);
        state->page_address = page;
}
void light_display_sh1107_command_set_scan_dir(struct display_device *dev, uint8_t data)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_SCAN_DIR + data);
}
void light_display_sh1107_command_set_display_clock(struct display_device *dev, uint8_t freq, uint8_t div)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_DISPLAY_CLK);
        light_display_ioport_send_command_byte(state->io_ctx, ((freq & 0x0F) << 4) + (div & 0x0F));
}
void light_display_sh1107_command_set_charge_periods(struct display_device *dev, uint8_t pre, uint8_t dis)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_CHARGE_PERIODS);
        light_display_ioport_send_command_byte(state->io_ctx, (pre & 0x0F) + ((dis & 0x0F) << 4));
}
void light_display_sh1107_command_set_vcom_deselect(struct display_device *dev, uint8_t data)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_VCOMH);
        light_display_ioport_send_command_byte(state->io_ctx, data);
}
void light_display_sh1107_command_set_start_line(struct display_device *dev, uint8_t addr)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_SET_DISPLAY_START);
        light_display_ioport_send_command_byte(state->io_ctx, addr);
}
void light_display_sh1107_command_rmw_begin(struct display_device *dev)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_RMW_BEGIN);
}
void light_display_sh1107_command_rmw_end(struct display_device *dev)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_RMW_END);
}
void light_display_sh1107_command_no_op(struct display_device *dev)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_command_byte(state->io_ctx, SH1107_CMD_NOP);
}
void light_display_sh1107_write_data(struct display_device *dev, uint8_t data)
{
        struct sh1107_state *state = (struct sh1107_state *) dev->driver_ctx->state;
        light_display_ioport_send_data_byte(state->io_ctx, data);

        // increment address counter, to track state of driver's internal counter
        switch (state->addrmode)
        {
        case SH1107_ADDRMODE_PAGE:
                state->column_address = (state->column_address + 1) % state->n_columns;
                break;
        case SH1107_ADDRMODE_VERTICAL:
                state->page_address = (state->page_address + 1) % state->n_pages;
                break;
        }
}
uint16_t light_display_sh1107_y_to_pages(uint16_t y)
{
        return (y / 8) + ((y % 8)? 1 : 0);
}
uint16_t light_display_sh1107_x_to_columns(uint16_t x)
{
        return x;
}
