#include <light_display.h>

#include "light_display_internal.h"

static void _device_root_child_add(struct light_object *obj, struct light_object *child)
{
        struct display_device_root *root = to_display_device_root(obj);
        struct display_device *dev = to_display_device(child);
        root->device[dev->device_id] = dev;
}
static void _device_release(struct light_object *obj)
{
        light_free(to_display_device(obj));
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
        light_object_init(&device_root.header, &ltype_display_device_root);
        light_object_add(&device_root.header, NULL, "root_device");
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
// every entry point below that isn't update_async_poll() itself must not run while an
// async update is in flight -- reset/clear/a plain sync update would stomp CS/column-address
// state out from under an in-progress DMA transfer. rather than duplicate this guard in
// every driver, it lives here once: block (briefly) until any in-flight update finishes
// before doing anything else. this is a bounded, cooperative drain (repeatedly calling the
// driver's own poll function), not a busy-wait on hardware -- the same mechanism the
// scheduler's periodic task uses, just run inline instead of once-per-tick
static void _light_display_drain_async(struct display_device *dev)
{
        const struct display_driver *drv = dev->driver_ctx->driver;
        if(!drv->update_async_poll || !drv->update_async_is_active)
                return;
        while(drv->update_async_is_active(dev)) {
                drv->update_async_poll(dev);
        }
}
void light_display_command_update(struct display_device *dev)
{
        light_debug("device: %s", dev->header.id);
        _light_display_drain_async(dev);
        dev->driver_ctx->driver->update(dev);
}
void light_display_command_update_async(struct display_device *dev)
{
        light_debug("device: %s", dev->header.id);
        const struct display_driver *drv = dev->driver_ctx->driver;
        if(!drv->update_async_start) {
                // driver hasn't implemented async yet -- fall back to a normal blocking
                // update rather than silently doing nothing
                drv->update(dev);
                return;
        }
        drv->update_async_start(dev);
}
bool light_display_update_in_progress(struct display_device *dev)
{
        const struct display_driver *drv = dev->driver_ctx->driver;
        if(!drv->update_async_is_active)
                return false;
        return drv->update_async_is_active(dev);
}
bool light_display_render_context_busy(struct rend_context *ctx)
{
        for(uint16_t i = 0; i < next_device_id; i++) {
                struct display_device *dev = device_root.device[i];
                if(!dev || dev->render_ctx != ctx)
                        continue;
                if(light_display_update_in_progress(dev))
                        return true;
        }
        return false;
}
void light_display_poll_async_updates(void)
{
        for(uint16_t i = 0; i < next_device_id; i++) {
                struct display_device *dev = device_root.device[i];
                if(!dev)
                        continue;
                const struct display_driver *drv = dev->driver_ctx->driver;
                if(drv->update_async_poll)
                        drv->update_async_poll(dev);
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
