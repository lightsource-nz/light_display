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

struct display_device;
struct display_driver
{
        const uint8_t *name;
        struct display_driver_context *(*spawn_context)();
        void (*init_device)(struct display_device *);
        void (*reset)(struct display_device *);
        void (*update)(struct display_device *);
        // matches light_display_command_clear()'s own uint16_t -- previously uint8_t here,
        // which silently truncated every call through the mismatched function-pointer type
        // (harmless for 1bpp OLED drivers, which only ever used the low byte anyway, but
        // wrong for any 16bpp RGB565 driver that needs a real color, not just 0/1)
        void (*clear)(struct display_device *, uint16_t value);
        // all three optional -- NULL means the driver hasn't implemented async updates yet,
        // in which case light_display_command_update_async() falls back to the blocking
        // update() above
        void (*update_async_start)(struct display_device *);
        // advances the in-flight update as far as it can go right now; returns true once
        // the whole update has completed (or been aborted -- see the timeout handling in
        // individual drivers), false if there's still more to do
        bool (*update_async_poll)(struct display_device *);
        // pure query, no side effects -- unlike update_async_poll(), safe to call just to
        // check status without accidentally advancing the state machine
        bool (*update_async_is_active)(struct display_device *);
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
// completion in the background by light_display's own periodic task (see module.c). falls
// back to a normal blocking update() if the driver hasn't implemented async support.
// don't mutate dev->render_ctx->buffer while an update is in flight (rend has no
// double-buffering) -- check light_display_update_in_progress() first if unsure
extern void light_display_command_update_async(struct display_device *dev);
extern bool light_display_update_in_progress(struct display_device *dev);
// true if any registered display device currently rendering through ctx has an async
// update in flight. callers should check this before rend_context_swap_buffers(ctx) --
// swapping into a buffer a driver is still reading from would tear the in-flight frame
extern bool light_display_render_context_busy(struct rend_context *ctx);

#endif