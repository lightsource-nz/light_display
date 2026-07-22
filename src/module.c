#include <light.h>
#include <module/mod_light_display.h>
#include <module/mod_light_display_sh1107.h>
#include <module/mod_light_display_po13.h>

static void _module_event(const struct light_module *module, uint8_t event, void *arg);
Light_Module_Define(light_display_po13, _module_event,
                                &light_display,
                                &light_display_sh1107);

static void _module_event(const struct light_module *module, uint8_t event, void *arg)
{
}
