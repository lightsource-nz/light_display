#include <light_backlight.h>

#include "light_backlight_internal.h"

static void _module_event(const struct light_module *module, uint8_t event, void *arg);
Light_Module_Define(light_backlight, _module_event,
                                &light_core);

static uint8_t _module_task(struct light_application *app);
static void _module_event(const struct light_module *module, uint8_t event, void *arg)
{
        switch(event) {
                case LF_EVENT_MODULE_LOAD:
                light_backlight_init();
                // unlike light_ui and light_canvas, this DOES want a periodic task: a fade
                // has to keep advancing on its own once started, without the application
                // being obliged to drive it frame by frame
                light_module_register_periodic_task(&light_backlight, "light_backlight_task", _module_task);
                break;
                // TODO implement unregister for event hooks
                case LF_EVENT_MODULE_UNLOAD:
                break;
        }
}
static uint8_t _module_task(struct light_application *app)
{
        light_backlight_poll_devices();
        return LF_STATUS_RUN;
}
