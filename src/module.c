#include <light_display.h>

#include "light_display_internal.h"

static void _module_event(const struct light_module *module, uint8_t event);
Light_Module_Define(light_display, _module_event,
                                &rend,
                                &light_framework)

static uint8_t _module_task(struct light_application *app);
static void _module_event(const struct light_module *module, uint8_t event)
{
        switch(event) {
                case LF_EVENT_LOAD:
                light_display_init();
                light_module_register_periodic_task(&light_display, "light_display_task", _module_task);
                break;
                // TODO implement unregister for event hooks
                case LF_EVENT_UNLOAD:
                break; 
        }
}
static uint8_t _module_task(struct light_application *app)
{
        // TODO add tick counter and cycle timer, etc
        light_info("enter light-display module task, time=%dms, time since last run=%dms", 0, 0);
        
        return LF_STATUS_RUN;
}
