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
}
struct display_device *light_display_create_device(struct display_driver *driver, uint16_t width,
                                                uint16_t height, uint8_t bpp)
{
        struct display_device *dev = light_object_alloc(sizeof(struct display_device));
        struct display_driver_context *driver_ctx = driver->spawn_context();
        return light_display_init_device(dev, driver_ctx, width, height, bpp);
}
struct display_device *light_display_init_device(
                struct display_device *dev,
                struct display_driver_context *driver_ctx,
                uint16_t width, uint16_t height, uint8_t bpp)
{
        light_trace("(driver=%s, width=%d, height=%d, bpp=%d)",
                                driver_ctx->driver->name, width, height, bpp);
        light_object_init(&dev->header, &ltype_display_device);
        dev->device_id = next_device_id++;
        dev->width = width;
        dev->height = height;
        dev->bpp = bpp;
        dev->driver_ctx = driver_ctx;
        return dev;
}
void light_display_set_render_context(struct display_device *dev, struct rend_context *ctx)
{
        light_trace("device: %s, ctx: %s", dev->header.id, ctx->name);
        dev->render_ctx = ctx;
}
void light_display_add_device(struct display_device *dev, uint8_t *name)
{
        light_object_add(&dev->header, &device_root.header, "display_device:%s", name);
        light_info("new device initialized: '%s', driver: '%s'", dev->header.id, dev->driver_ctx->driver->name);
}
void light_display_command_init(struct display_device *dev)
{
        light_debug("device: %s", dev->header.id);
        dev->driver_ctx->driver->init_device(dev);
        dev->driver_ctx->driver->clear(dev, 0);
}
void light_display_command_update(struct display_device *dev)
{
        light_debug("device: %s", dev->header.id);
        dev->driver_ctx->driver->update(dev);
}
void light_display_command_reset(struct display_device *dev)
{
        light_debug("device: %s", dev->header.id);
        dev->driver_ctx->driver->reset(dev);
}
void light_display_command_clear(struct display_device *dev, uint16_t value)
{
        light_debug("device: %s, value: %d", dev->header.id, value);
        dev->driver_ctx->driver->clear(dev, value);
}
