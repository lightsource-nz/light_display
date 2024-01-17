#ifndef _LIGHT_DISPLAY_H
#define _LIGHT_DISPLAY_H

#include <light.h>
#include <module/mod_light_display.h>
#include <light_display_ioport.h>
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
        void (*clear)(struct display_device *, uint8_t value);
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
                                                uint16_t height, uint8_t bpp);
extern struct display_device *light_display_init_device(
                struct display_device *dev,
                struct display_driver_context *driver_ctx,
                uint16_t width, uint16_t height, uint8_t bpp);
extern void light_display_set_render_context(struct display_device *dev, struct rend_context *ctx);
extern void light_display_add_device(struct display_device *dev, uint8_t *name);
extern void light_display_command_init(struct display_device *dev);
extern void light_display_command_reset(struct display_device *dev);
extern void light_display_command_update(struct display_device *dev);
extern void light_display_command_clear(struct display_device *dev, uint16_t value);

#endif