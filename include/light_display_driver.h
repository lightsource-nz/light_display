#ifndef _LIGHT_DISPLAY_DRIVER_H
#define _LIGHT_DISPLAY_DRIVER_H

#ifndef _LIGHT_DISPLAY_H
#error __FILE__ must be included after <light_display.h>
#endif

#include <stdint.h>

#define ID_DISPLAY_DEVICE                       "light_display:device"
#define LIGHT_DISPLAY_BUFFERING                 CFG_LIGHT_DISPLAY_BUFFERING

struct display_device;
struct display_driver
{
        const uint8_t *name;
        void (*update)(struct display_device *);
        void (*clear)(struct display_device *, uint16_t value);
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
        struct display_ops *ops;
};

#define to_display_device(ptr) container_of(ptr, struct display_device, header)

extern void light_display_init();

extern struct display_device *light_display_create_device(uint8_t *name,uint16_t width,
                                                uint16_t height, uint8_t bpp, struct display_ops *ops);
extern struct display_device *light_display_init_device(struct display_device *dev, uint8_t *name,
                                uint16_t width, uint16_t height, uint8_t bpp, struct display_ops *ops);
extern void light_display_set_render_context(struct display_device *dev, struct rend_context *ctx);
extern void light_display_update(struct display_device *dev);
extern void light_display_clear(struct display_device *dev);

#endif