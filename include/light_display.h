#ifndef _LIGHT_DISPLAY_H
#define _LIGHT_DISPLAY_H

#include <light.h>
#include <light_ioport.h>
#include <rend.h>

#include <stdint.h>

#define ID_DISPLAY_DEVICE_ROOT                  "light_display:device_root"
#define ID_DISPLAY_DEVICE                       "light_display:device"
#define LIGHT_DISPLAY_BUFFERING                 CFG_LIGHT_DISPLAY_BUFFERING

#define LIGHT_DISPLAY_MAX_DEVICES               16

// an inclusive rectangle in PHYSICAL buffer coordinates (not the caller's logical,
// possibly rotated, rend space -- see light_display_command_update_region())
struct display_region {
        uint16_t x0;
        uint16_t y0;
        uint16_t x1;
        uint16_t y1;
};

struct display_device;
// drivers no longer implement an update state machine of their own. instead an update --
// full-screen or region-limited, blocking or async, all the same path -- is expressed as a
// sequence of "chunks", and the driver only answers three questions about the update
// currently described by dev->update_region: how many chunks it takes, how to push chunk
// N, and whether the chunk in flight has landed. everything else (the in-progress flag,
// start timestamp, source-buffer capture, chunk index, re-entrancy guard, timeout and
// abort) is owned by light_display and was previously duplicated near-verbatim in every
// driver that supported async at all
struct display_driver
{
        const uint8_t *name;
        struct display_driver_context *(*spawn_context)();
        void (*init_device)(struct display_device *);
        void (*reset)(struct display_device *);
        // matches light_display_command_clear()'s own uint16_t -- previously uint8_t here,
        // which silently truncated every call through the mismatched function-pointer type
        // (harmless for 1bpp OLED drivers, which only ever used the low byte anyway, but
        // wrong for any 16bpp RGB565 driver that needs a real color, not just 0/1)
        void (*clear)(struct display_device *, uint16_t value);

        // how many chunks the update described by dev->update_region takes. the chunking
        // axis is the driver's own choice -- whatever its addressing mode makes natural
        // (SH1107 sequences by column, SH1106 by page, ST7789 by row, or 1 for a
        // whole-region contiguous burst). returning 0 means "nothing to send"
        uint16_t (*async_chunk_count)(struct display_device *);
        // pushes chunk chunk_index of dev->update_region, reading pixels from
        // dev->update_source_buffer (captured once at start, so a mid-update
        // rend_context_swap_buffers() can't tear the transfer). must start the transfer
        // without waiting for it -- completion is reported through async_chunk_complete()
        void (*async_kick)(struct display_device *, uint16_t chunk_index);
        // has the chunk most recently passed to async_kick() finished transferring?
        // usually a thin forward to light_ioport_burst_is_complete() on the driver's own
        // io_context, which light_display has no visibility into
        bool (*async_chunk_complete)(struct display_device *);

        // upper bound on one whole update, after which it is abandoned with an error.
        // data rather than a function because the logic around it was identical in every
        // driver and only the constant ever differed
        uint32_t async_timeout_ms;
        // how many chunks one poll may complete before yielding back to the scheduler.
        // 0 means "no limit": drain the entire update in a single poll, which is what a
        // driver wants when its chunks are only a few bytes each and yielding per chunk
        // would spend a whole scheduler tick moving a handful of bytes. a nonzero budget
        // keeps a long update from monopolising the tick
        uint16_t async_chunks_per_poll;
};
struct display_driver_context
{
        const struct display_driver *driver;
        const void *state;
};

struct display_device {
        struct light_object header;
        uint8_t device_id;
        uint16_t width;
        uint16_t height;
        uint8_t bpp;
        struct rend_context *render_ctx;
        struct display_driver_context *driver_ctx;

        // update state, owned by light_display and read by drivers -- see struct
        // display_driver above for why it lives here rather than in driver state
        struct display_region update_region;
        const uint8_t *update_source_buffer;
        // when the chunk currently in flight was kicked -- the async_timeout_ms deadline
        // is measured from here, so it bounds a single chunk rather than a whole update.
        // an update can legitimately sit parked for a long time between polls, which says
        // nothing about whether the transport is stuck
        uint32_t update_chunk_time_ms;
        uint16_t update_chunk_index;
        uint16_t update_chunk_count;
        bool update_in_progress;
};
struct display_device_root {
        struct light_object header;
        struct display_device *device[LIGHT_DISPLAY_MAX_DEVICES];
};

#define to_display_device_root(ptr) container_of(ptr, struct display_device_root, header)
#define to_display_device(ptr) container_of(ptr, struct display_device, header)

extern void light_display_init();

extern struct display_device_root *light_display_device_get_root();
extern struct display_device *light_display_device_get(uint8_t *name);
extern struct display_device *light_display_create_device(struct display_driver *driver, uint16_t width,
                                                uint16_t height, uint8_t bpp, uint8_t *format, ...);
extern struct display_device *light_display_create_device_va(struct display_driver *driver, uint16_t width,
                                                uint16_t height, uint8_t bpp, uint8_t *format, va_list args);
extern struct display_device *light_display_init_device(
                struct display_device *dev,
                struct display_driver_context *driver_ctx,
                uint16_t width, uint16_t height, uint8_t bpp,
                uint8_t *format, ...);
extern struct display_device *light_display_init_device_va(
                struct display_device *dev,
                struct display_driver_context *driver_ctx,
                uint16_t width, uint16_t height, uint8_t bpp,
                uint8_t *format, va_list args);
extern void light_display_set_render_context(struct display_device *dev, struct rend_context *ctx);
extern void light_display_command_init(struct display_device *dev);
extern void light_display_command_reset(struct display_device *dev);
extern void light_display_command_update(struct display_device *dev);
extern void light_display_command_clear(struct display_device *dev, uint16_t value);
// starts a non-blocking update: returns immediately, and the transfer is driven to
// completion in the background by light_display's own periodic task (see module.c).
// don't mutate dev->render_ctx->buffer while an update is in flight -- either check
// light_display_update_in_progress() first, or render through a double-buffered context
// (rend_context_enable_double_buffer()) and gate the swap on
// light_display_render_context_busy()
extern void light_display_command_update_async(struct display_device *dev);
// as above, but pushes only the pixels inside the given rectangle. p0/p1 are inclusive
// corners in LOGICAL (rend) coordinates -- the same space the caller draws in, so a
// rotated context is handled here rather than by every caller. drivers honour the region
// at whatever granularity their addressing scheme makes natural and may round it outward
// (SH1107, which sequences whole columns, keeps the x extent and rounds y to the full
// panel height), so this is an optimisation, never a correctness guarantee about what
// stays untouched.
//
// the caller is responsible for the buffer outside the region already matching what the
// panel shows -- anything else that changed since the last update will not be sent. use
// the full-screen update above whenever that isn't certain
extern void light_display_command_update_region(struct display_device *dev,
                                                rend_point2d p0, rend_point2d p1);
extern void light_display_command_update_region_async(struct display_device *dev,
                                                rend_point2d p0, rend_point2d p1);
extern bool light_display_update_in_progress(struct display_device *dev);
// drives any in-flight async update on this device to completion and returns once the
// render buffer is no longer being read. callers rendering into a single (non-double-
// buffered) context must call this before mutating that buffer -- an async update reads
// it well after the call that started it returned. cooperative, not a hardware spin: it
// runs the same poll the scheduler's periodic task does, just inline
extern void light_display_wait_for_update(struct display_device *dev);
// true if any registered display device currently rendering through ctx has an async
// update in flight. callers should check this before rend_context_swap_buffers(ctx) --
// swapping into a buffer a driver is still reading from would tear the in-flight frame
extern bool light_display_render_context_busy(struct rend_context *ctx);

#endif