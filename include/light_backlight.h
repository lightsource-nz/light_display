#ifndef _LIGHT_BACKLIGHT_H
#define _LIGHT_BACKLIGHT_H

#include <light.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#define ID_BACKLIGHT_DEVICE_ROOT                "light_backlight:device_root"
#define ID_BACKLIGHT_DEVICE                     "light_backlight:device"

#define LIGHT_BACKLIGHT_MAX_DEVICES             4

// brightness is per-mille, 0..1000. finer than a percentage on purpose: a fade stepping in
// whole percent visibly banks at the dim end, where the eye is most sensitive to change,
// and per-mille costs nothing since it is all integer anyway
#define LIGHT_BACKLIGHT_LEVEL_MAX               1000

struct backlight_device;
struct backlight_driver
{
        const uint8_t *name;
        struct backlight_driver_context *(*spawn_context)();
        //   frees whatever spawn_context() allocated. Called when the device holding that
        // context is released, so a context outlives exactly the device it was spawned for.
        // OPTIONAL: a driver whose context is not heap-allocated leaves this NULL and the
        // release path skips it
        void (*destroy_context)(struct backlight_driver_context *ctx);
        void (*init_device)(struct backlight_device *);
        // applies a level in 0..LIGHT_BACKLIGHT_LEVEL_MAX. the driver deals in nothing else
        // -- fading, timing and clamping are light_backlight's, because they would otherwise
        // be identical in every driver
        void (*set_level)(struct backlight_device *, uint16_t level);
};
struct backlight_driver_context
{
        const struct backlight_driver *driver;
        const void *state;
};

struct backlight_device {
        struct light_object header;
        uint8_t device_id;
        // the level currently applied to the hardware
        uint16_t level;
        struct backlight_driver_context *driver_ctx;

        // --- fade state, advanced from light_backlight_poll_devices() ---
        bool fading;
        uint16_t fade_from;
        uint16_t fade_to;
        uint32_t fade_start_ms;
        uint16_t fade_duration_ms;
};
struct backlight_device_root {
        struct light_object header;
        struct backlight_device *device[LIGHT_BACKLIGHT_MAX_DEVICES];
};

#define to_backlight_device_root(ptr) container_of(ptr, struct backlight_device_root, header)
#define to_backlight_device(ptr) container_of(ptr, struct backlight_device, header)

extern void light_backlight_init();

extern struct backlight_device_root *light_backlight_device_get_root();
extern struct backlight_device *light_backlight_create_device(struct backlight_driver *driver,
                                                uint8_t *format, ...);
extern struct backlight_device *light_backlight_create_device_va(struct backlight_driver *driver,
                                                uint8_t *format, va_list args);
// lower-level entry point, same rationale as light_touch/light_button's: a driver's own
// state (which pin, what polarity) must be attached before the device joins the object tree,
// since that add synchronously triggers init_device()
extern struct backlight_device *light_backlight_init_device(
                struct backlight_device *dev,
                struct backlight_driver_context *driver_ctx,
                uint8_t *format, ...);
extern struct backlight_device *light_backlight_init_device_va(
                struct backlight_device *dev,
                struct backlight_driver_context *driver_ctx,
                uint8_t *format, va_list args);
extern void light_backlight_command_init(struct backlight_device *dev);

// sets the level immediately, cancelling any fade in progress -- an explicit level is a
// statement about where the brightness should be NOW, so letting a fade keep moving it
// afterwards would be surprising
extern void light_backlight_set_level(struct backlight_device *dev, uint16_t level);
// eases to `level` over `duration_ms`. a zero duration is the same as set_level(). starting
// a fade while one is running redirects from wherever the brightness currently is, rather
// than jumping back to re-run from the old start
extern void light_backlight_fade_to(struct backlight_device *dev, uint16_t level,
                                                uint16_t duration_ms);
static inline uint16_t light_backlight_get_level(const struct backlight_device *dev)
{
        return dev->level;
}
static inline bool light_backlight_is_fading(const struct backlight_device *dev)
{
        return dev->fading;
}

// --- built-in PWM driver ---
// a backlight on a pin is the degenerate case rather than a vendor part, so this lives here
// instead of in a light_backlight_<chip> repo of its own -- the same call light_button's GPIO
// driver makes. active_low suits panels whose enable line sinks rather than sources
extern struct backlight_driver *light_backlight_driver_pwm();
extern struct backlight_device *light_backlight_pwm_create_device(uint8_t *name, uint8_t pin,
                                                bool active_low);

#endif
