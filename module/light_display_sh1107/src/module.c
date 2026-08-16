#include <light.h>
#include <module/mod_light_display.h>
#include <module/mod_light_display_sh1107.h>

static void _module_event(const struct light_module *module, uint8_t event, void *arg);
Light_Module_Define(light_display_sh1107, _module_event,
                                &light_display);

static void _module_event(const struct light_module *module, uint8_t event, void *arg)
{
}