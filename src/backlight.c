#include <light_backlight.h>
#include <light_platform.h>

#include "light_backlight_internal.h"

static void _device_root_child_add(struct light_object *obj, struct light_object *child)
{
        struct backlight_device_root *root = to_backlight_device_root(obj);
        struct backlight_device *dev = to_backlight_device(child);
        root->device[dev->device_id] = dev;
}
static void _device_release(struct light_object *obj)
{
        light_free(to_backlight_device(obj));
}
static void _device_add(struct light_object *obj, struct light_object *parent) {
        struct backlight_device *dev = to_backlight_device(obj);
        light_debug("name=%s", dev->header.id);
        light_backlight_command_init(dev);
}
// singleton container object for backlight_device objects
static struct lobj_type ltype_backlight_device_root = (struct lobj_type) {
        .id = ID_BACKLIGHT_DEVICE_ROOT,
        .release = NULL,
        .evt_child_add = _device_root_child_add
};
static struct lobj_type ltype_backlight_device = (struct lobj_type) {
        .id = ID_BACKLIGHT_DEVICE,
        .release = _device_release,
        .evt_add = _device_add
};
static struct backlight_device_root device_root;

static volatile uint16_t next_device_id;

void light_backlight_init()
{
        next_device_id = 0;
        light_object_init(&device_root.header, &ltype_backlight_device_root);
        light_object_add(&device_root.header, NULL, "root_device");
}
struct backlight_device_root *light_backlight_device_get_root()
{
        return &device_root;
}
struct backlight_device *light_backlight_create_device(struct backlight_driver *driver,
                                                uint8_t *format, ...)
{
        va_list vargs;

        va_start(vargs, format);
        struct backlight_device *dev = light_backlight_create_device_va(driver, format, vargs);
        va_end(vargs);
        return dev;
}
struct backlight_device *light_backlight_create_device_va(struct backlight_driver *driver,
                                                uint8_t *format, va_list args)
{
        struct backlight_device *dev = light_object_alloc(sizeof(struct backlight_device));
        struct backlight_driver_context *driver_ctx = driver->spawn_context();

        return light_backlight_init_device_va(dev, driver_ctx, format, args);
}
struct backlight_device *light_backlight_init_device(
                struct backlight_device *dev,
                struct backlight_driver_context *driver_ctx,
                uint8_t *format, ...)
{
        va_list vargs;

        va_start(vargs, format);
        struct backlight_device *out = light_backlight_init_device_va(dev, driver_ctx, format, vargs);
        va_end(vargs);
        return out;
}
struct backlight_device *light_backlight_init_device_va(
                struct backlight_device *dev,
                struct backlight_driver_context *driver_ctx,
                uint8_t *format, va_list args)
{
        light_trace("(driver=%s)", driver_ctx->driver->name);
        // TODO: this should be an ASSERT statement
        if(next_device_id >= LIGHT_BACKLIGHT_MAX_DEVICES) {
                light_error("could not create new device: max devices reached (%d)", next_device_id);
                return NULL;
        }
        uint8_t device_id = next_device_id++;
        light_object_init(&dev->header, &ltype_backlight_device);
        dev->device_id = device_id;
        dev->driver_ctx = driver_ctx;
        // light_object_alloc() doesn't zero. an uninitialised `fading` would have the first
        // poll interpolate between two garbage levels and visibly flash the panel before
        // anything had asked for a fade at all
        dev->level = LIGHT_BACKLIGHT_LEVEL_MAX;
        dev->fading = false;
        dev->fade_from = LIGHT_BACKLIGHT_LEVEL_MAX;
        dev->fade_to = LIGHT_BACKLIGHT_LEVEL_MAX;
        dev->fade_start_ms = 0;
        dev->fade_duration_ms = 0;

        light_object_add_va(&dev->header, &device_root.header, format, args);
        return dev;
}
void light_backlight_command_init(struct backlight_device *dev)
{
        light_debug("device: %s", dev->header.id);
        dev->driver_ctx->driver->init_device(dev);
        // the driver has only just configured the pin, so push the starting level through
        // rather than leaving the hardware at whatever the peripheral powered up with
        dev->driver_ctx->driver->set_level(dev, dev->level);
}

static uint16_t _clamp(uint16_t level)
{
        return level > LIGHT_BACKLIGHT_LEVEL_MAX ? LIGHT_BACKLIGHT_LEVEL_MAX : level;
}

void light_backlight_set_level(struct backlight_device *dev, uint16_t level)
{
        level = _clamp(level);
        dev->fading = false;
        dev->level = level;
        dev->driver_ctx->driver->set_level(dev, level);
}

void light_backlight_fade_to(struct backlight_device *dev, uint16_t level, uint16_t duration_ms)
{
        level = _clamp(level);
        if(!duration_ms || level == dev->level) {
                light_backlight_set_level(dev, level);
                return;
        }
        // from wherever the brightness actually IS, which mid-fade is not where the previous
        // fade started -- redirecting has to look continuous, not snap back and re-run
        dev->fade_from = dev->level;
        dev->fade_to = level;
        dev->fade_start_ms = light_platform_get_time_since_init();
        dev->fade_duration_ms = duration_ms;
        dev->fading = true;
}

// advances one device's fade. driven off the clock rather than stepped per tick, so the fade
// takes the same real time whatever the scheduler is doing -- the same reasoning the circle
// animation and the UI rotation both use
static void _advance_fade(struct backlight_device *dev, uint32_t now)
{
        if(!dev->fading)
                return;

        uint32_t elapsed = now - dev->fade_start_ms;
        uint16_t level;
        if(elapsed >= dev->fade_duration_ms) {
                level = dev->fade_to;
                dev->fading = false;
        } else {
                int32_t span = (int32_t)dev->fade_to - (int32_t)dev->fade_from;
                level = (uint16_t)((int32_t)dev->fade_from
                                + (span * (int32_t)elapsed) / (int32_t)dev->fade_duration_ms);
        }

        // only touch the hardware when the value actually moves: a fade across a few hundred
        // ms is polled far more often than it has distinct levels to show
        if(level == dev->level)
                return;
        dev->level = level;
        dev->driver_ctx->driver->set_level(dev, level);
}

void light_backlight_poll_devices(void)
{
        uint32_t now = light_platform_get_time_since_init();
        for(uint16_t i = 0; i < next_device_id; i++) {
                struct backlight_device *dev = device_root.device[i];
                if(!dev)
                        continue;
                _advance_fade(dev, now);
        }
}
