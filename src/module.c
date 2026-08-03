#include <light_display.h>

#include "light_display_internal.h"

static void _module_event(const struct light_module *module, uint8_t event, void *arg);
Light_Module_Define(light_display, _module_event,
                                &rend,
                                &light_core);

static uint8_t _module_task(struct light_application *app);
static void _module_event(const struct light_module *module, uint8_t event, void *arg)
{
        switch(event) {
                case LF_EVENT_MODULE_LOAD:
                light_display_init();
                light_module_register_periodic_task(&light_display, "light_display_task", _module_task);
                break;
                // TODO implement unregister for event hooks
                case LF_EVENT_MODULE_UNLOAD:
                break; 
        }
}
static uint8_t _module_task(struct light_application *app)
{
        // TODO add tick counter and cycle timer, etc
        return LF_STATUS_RUN;
}
