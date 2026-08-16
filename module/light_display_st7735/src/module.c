#include <light.h>
#include <module/mod_light_display.h>

static void _module_event(const struct light_module *module, uint8_t event, void *arg);
Light_Module_Define(light_display_st7735, _module_event,
                                &light_display,
                                &light_core);

static void _module_event(const struct light_module *module, uint8_t event, void *arg)
{
}
