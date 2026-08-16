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
                //   the mirror image of LOAD above, in reverse: the task comes out of the
                // schedule BEFORE the state it polls is torn down, or the next tick would run
                // _module_task() against released devices
                case LF_EVENT_MODULE_UNLOAD:
                light_module_unregister_periodic_task(&light_display, _module_task);
                light_display_shutdown();
                break;
        }
}
static uint8_t _module_task(struct light_application *app)
{
        light_display_poll_async_updates();
        return LF_STATUS_RUN;
}
