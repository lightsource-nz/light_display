#include <light_display.h>
#include <light_platform.h>

#include "light_display_internal.h"

static void _light_display_set_region_full(struct display_device *dev);
static void _light_display_drain_async(struct display_device *dev);

static void _device_root_child_add(struct light_object *obj, struct light_object *child)
{
        struct display_device_root *root = to_display_device_root(obj);
        struct display_device *dev = to_display_device(child);
        root->device[dev->device_id] = dev;
}
static void _device_release(struct light_object *obj)
{
        struct display_device *dev = to_display_device(obj);
        //   the driver context was spawned for this device alone, so it goes with it. Without
        // this the device is reclaimed and its context -- plus whatever driver state hangs
        // off it -- is not, a leak that only becomes visible once teardown is exercised
        if(dev->driver_ctx && dev->driver_ctx->driver->destroy_context)
                dev->driver_ctx->driver->destroy_context(dev->driver_ctx);
        light_free(dev);
}
static void _device_add(struct light_object *obj, struct light_object *parent) {
        struct display_device *dev = to_display_device(obj);
        light_debug("name=%s", dev->header.id);
        dev->driver_ctx->driver->init_device(dev);
        light_display_command_init(dev);
}
// singleton container object for display_device objects
static struct lobj_type ltype_display_device_root = (struct lobj_type) {
        .id = ID_DISPLAY_DEVICE_ROOT,
        .release = NULL,
        .evt_child_add = _device_root_child_add
};
static struct lobj_type ltype_display_device = (struct lobj_type) {
        .id = ID_DISPLAY_DEVICE,
        .release = _device_release,
        .evt_add = _device_add
};
static struct display_device_root device_root;

static volatile uint16_t next_device_id;

void light_display_init()
{
        next_device_id = 0;
        light_object_init_static(&device_root.header, &ltype_display_device_root);
        light_object_add(&device_root.header, NULL, "root_device");
}
//   the exact inverse of light_display_init(), for LF_EVENT_MODULE_UNLOAD. Devices are torn
// down in the reverse of the order they were created, so anything set up against an earlier
// device still has it while it goes.
//
//   each device needs BOTH calls: del() detaches it and hands back the reference the root took
// when it was added, put() drops the one it has held since it was created. Only the second
// takes the count to zero, and only that runs _device_release() to free it.
//
//   the driver context spawned for each device goes with it: _device_release() calls the
// driver's destroy_context(), which is spawn_context()'s counterpart
void light_display_shutdown()
{
        light_trace("tearing down %d display device(s)", next_device_id);
        for(uint16_t i = next_device_id; i > 0; i--) {
                struct display_device *dev = device_root.device[i - 1];
                if(!dev)
                        continue;
                device_root.device[i - 1] = NULL;
                light_object_del(&dev->header);
                light_object_put(&dev->header);
        }
        next_device_id = 0;

        //   the root is file-scope storage, marked static at init, so this drops its count to
        // zero without releasing anything -- there is nothing to free, and its release hook is
        // NULL in any case. It is here so the count is balanced rather than left dangling
        light_object_put(&device_root.header);
}
struct display_device_root *light_display_device_get_root()
{
        return &device_root;
}
struct display_device *light_display_create_device(struct display_driver *driver, uint16_t width,
                                                uint16_t height, uint8_t bpp, uint8_t *format, ...)
{
        struct display_device *dev = light_object_alloc(sizeof(struct display_device));
        struct display_driver_context *driver_ctx = driver->spawn_context();
        
        va_list vargs;

        va_start(vargs, format);
        return light_display_init_device_va(dev, driver_ctx, width, height, bpp, format, vargs);
        va_end(vargs);
}
struct display_device *light_display_init_device(
                struct display_device *dev,
                struct display_driver_context *driver_ctx,
                uint16_t width, uint16_t height, uint8_t bpp, uint8_t *format, ...)
{
        
        va_list vargs;

        va_start(vargs, format);
        return light_display_init_device_va(dev, driver_ctx, width, height, bpp, format, vargs);
        va_end(vargs);
}
struct display_device *light_display_init_device_va(
                struct display_device *dev,
                struct display_driver_context *driver_ctx,
                uint16_t width, uint16_t height, uint8_t bpp, uint8_t *format, va_list args)
{
        light_trace("(driver=%s, width=%d, height=%d, bpp=%d)",
                                driver_ctx->driver->name, width, height, bpp);
        // TODO: this should be an ASSERT statement
        if(next_device_id >= LIGHT_DISPLAY_MAX_DEVICES) {
                light_error("could not create new device: max devices reached (%d)", next_device_id);
                return NULL;
        }
        uint8_t device_id = next_device_id++;
        light_object_init(&dev->header, &ltype_display_device);
        dev->device_id = device_id;
        dev->width = width;
        dev->height = height;
        dev->bpp = bpp;
        dev->driver_ctx = driver_ctx;
        // light_object_alloc() doesn't zero, and _device_add() below reaches the driver's
        // init_device()/clear() while this is still the only initialisation that has run --
        // an uninitialised update_in_progress would make the very first drain spin against
        // a driver that has nothing in flight
        dev->update_in_progress = false;
        dev->update_source_buffer = NULL;
        dev->update_chunk_index = 0;
        dev->update_chunk_count = 0;
        dev->update_chunks_per_poll = 0;
        _light_display_set_region_full(dev);

        light_object_add_va(&dev->header, &device_root.header, format, args);
        return dev;
}
void light_display_set_render_context(struct display_device *dev, struct rend_context *ctx)
{
        light_trace("device: %s, ctx: %s", dev->header.id, ctx->name);
        dev->render_ctx = ctx;
}
void light_display_command_init(struct display_device *dev)
{
        light_debug("device: %s", dev->header.id);
        dev->driver_ctx->driver->init_device(dev);
        dev->driver_ctx->driver->clear(dev, 0);
}
// advances an in-flight update. returns true once it has completed (or been abandoned on
// timeout), false if there is still more to send. this is the whole async state machine
// that every async-capable driver used to carry its own near-identical copy of
static bool _light_display_update_poll(struct display_device *dev)
{
        const struct display_driver *drv = dev->driver_ctx->driver;
        if(!dev->update_in_progress)
                return true;

        const uint16_t budget = dev->update_chunks_per_poll;
        uint16_t completed = 0;
        while(1) {
                if(!drv->async_chunk_complete(dev)) {
                        // the deadline is deliberately only consulted HERE, on the path
                        // where we are actually waiting on hardware. an update legitimately
                        // sits parked between polls -- it may even be started before the
                        // scheduler is running at all (crossfire kicks its first paint from
                        // module load) -- so wall-clock elapsed since the update began says
                        // nothing about whether the transport is stuck. checking it against
                        // a completed chunk would abort a perfectly healthy update purely
                        // because the caller took a while to come back and poll
                        if(light_platform_get_time_since_init() - dev->update_chunk_time_ms > drv->async_timeout_ms) {
                                // note this abandons light_display's bookkeeping without
                                // cancelling whatever the transport still has in flight --
                                // there's no ioport abort primitive to call, and in practice
                                // this only fires when the bus is already wedged
                                light_error("async update timed out for device '%s', aborting", dev->header.id);
                                dev->update_in_progress = false;
                                return true;
                        }
                        // a chunk that was only just kicked is essentially never complete
                        // yet, so whether to spin here or hand the tick back is the whole
                        // difference between the two kinds of driver. spinning is right
                        // when chunks are small and numerous (a column is tens of
                        // microseconds of bus time, and yielding per chunk would make a
                        // sweep take as many scheduler ticks as it has columns); yielding
                        // is right when a chunk is a large transfer worth overlapping with
                        // real work. the budget caps how long a spin can run before we
                        // yield anyway, so no driver can monopolise the tick
                        if(budget == 0 || completed >= budget)
                                return false;
                        continue;
                }
                completed++;
                dev->update_chunk_index++;
                if(dev->update_chunk_index >= dev->update_chunk_count) {
                        dev->update_in_progress = false;
                        return true;
                }
                drv->async_kick(dev, dev->update_chunk_index);
                // restamped per chunk, so async_timeout_ms bounds one chunk rather than a
                // whole update -- the useful question is "has this transfer stalled", not
                // "how long has this update been outstanding"
                dev->update_chunk_time_ms = light_platform_get_time_since_init();
        }
}
// no entry point that touches the panel may run while an update is in flight -- a
// reset/clear, or starting a second update, would stomp CS and the chip's address
// registers out from under a transfer the transport is still streaming. rather than
// duplicate that guard in every driver it lives here once: cooperatively drive any
// in-flight update to completion first. bounded work, not a hardware busy-wait -- the
// same poll the scheduler's periodic task runs, just inline instead of once-per-tick
static void _light_display_drain_async(struct display_device *dev)
{
        while(dev->update_in_progress)
                _light_display_update_poll(dev);
}
static void _light_display_set_region_full(struct display_device *dev)
{
        dev->update_region.x0 = 0;
        dev->update_region.y0 = 0;
        dev->update_region.x1 = dev->width - 1;
        dev->update_region.y1 = dev->height - 1;
}
// converts an inclusive LOGICAL rect into the equivalent inclusive PHYSICAL one, clamped
// to the panel. rotation/flip are rend's business, so the mapping is asked of rend rather
// than reimplemented here (and drivers stay in physical space, which is where they
// already address the buffer)
static void _light_display_set_region_logical(struct display_device *dev,
                                                rend_point2d p0, rend_point2d p1)
{
        rend_point2d min, max;
        rend_transform_rect(dev->render_ctx, p0, p1, &min, &max);

        // BOTH corners are clamped, not just the far one. a logical coordinate outside the
        // canvas transforms to a negative physical coordinate, which rend_point2d's uint16_t
        // turns into a huge positive value -- clamping only max then leaves x0 > x1, and an
        // inverted region has a meaningless chunk count. the update would never complete,
        // update_in_progress would never clear, and every later frame would be refused
        // because the render context looks permanently busy: a frozen display rather than
        // one dropped frame. rend_transform_rect() guarantees min <= max componentwise, so
        // clamping both against the same bound cannot itself invert them
        dev->update_region.x0 = min.x < dev->width ? min.x : dev->width - 1;
        dev->update_region.y0 = min.y < dev->height ? min.y : dev->height - 1;
        dev->update_region.x1 = max.x < dev->width ? max.x : dev->width - 1;
        dev->update_region.y1 = max.y < dev->height ? max.y : dev->height - 1;
}
static void _light_display_update_start(struct display_device *dev)
{
        const struct display_driver *drv = dev->driver_ctx->driver;

        dev->update_in_progress = true;
        dev->update_chunk_time_ms = light_platform_get_time_since_init();
        // captured once, so that a rend_context_swap_buffers() partway through can't leave
        // later chunks reading from a buffer the app has started redrawing
        dev->update_source_buffer = dev->render_ctx->buffer;
        dev->update_chunk_index = 0;
        dev->update_chunk_count = drv->async_chunk_count(dev);
        // asked per update, not per poll: how this region ended up chunked determines
        // whether spinning or yielding is the right wait
        dev->update_chunks_per_poll = drv->async_chunks_per_poll(dev);

        if(dev->update_chunk_count == 0) {
                dev->update_in_progress = false;
                return;
        }
        drv->async_kick(dev, 0);
}
void light_display_command_update(struct display_device *dev)
{
        // trace, not debug -- fires every frame on any continuously-redrawing display,
        // flooding the console at DEBUG level otherwise
        light_trace("device: %s", dev->header.id);
        _light_display_drain_async(dev);
        _light_display_set_region_full(dev);
        _light_display_update_start(dev);
        _light_display_drain_async(dev);
}
void light_display_command_update_async(struct display_device *dev)
{
        light_trace("device: %s", dev->header.id);
        _light_display_drain_async(dev);
        _light_display_set_region_full(dev);
        _light_display_update_start(dev);
}
void light_display_command_update_region(struct display_device *dev,
                                                rend_point2d p0, rend_point2d p1)
{
        light_trace("device: %s", dev->header.id);
        _light_display_drain_async(dev);
        _light_display_set_region_logical(dev, p0, p1);
        _light_display_update_start(dev);
        _light_display_drain_async(dev);
}
void light_display_command_update_region_async(struct display_device *dev,
                                                rend_point2d p0, rend_point2d p1)
{
        light_trace("device: %s", dev->header.id);
        _light_display_drain_async(dev);
        _light_display_set_region_logical(dev, p0, p1);
        _light_display_update_start(dev);
}
bool light_display_update_in_progress(struct display_device *dev)
{
        return dev->update_in_progress;
}
void light_display_wait_for_update(struct display_device *dev)
{
        _light_display_drain_async(dev);
}
bool light_display_render_context_busy(struct rend_context *ctx)
{
        for(uint16_t i = 0; i < next_device_id; i++) {
                struct display_device *dev = device_root.device[i];
                if(!dev || dev->render_ctx != ctx)
                        continue;
                if(dev->update_in_progress)
                        return true;
        }
        return false;
}
void light_display_poll_async_updates(void)
{
        for(uint16_t i = 0; i < next_device_id; i++) {
                struct display_device *dev = device_root.device[i];
                if(!dev || !dev->update_in_progress)
                        continue;
                _light_display_update_poll(dev);
        }
}
void light_display_command_reset(struct display_device *dev)
{
        light_debug("device: %s", dev->header.id);
        _light_display_drain_async(dev);
        dev->driver_ctx->driver->reset(dev);
}
void light_display_command_clear(struct display_device *dev, uint16_t value)
{
        light_debug("device: %s, value: %d", dev->header.id, value);
        _light_display_drain_async(dev);
        dev->driver_ctx->driver->clear(dev, value);
}
