#include<light_draw.h>
#include<stdio.h>

#include "TypeLightSans_ttf_16px_font.h"

#ifdef __HAVE_RP2_HW
#   include <pico/stdio.h>
#endif

void light_draw_demo_event(const struct light_module *module, uint8_t event, void *arg);
static uint8_t light_draw_demo_main(struct light_application *app);

Light_Application_Define(light_draw_demo, light_draw_demo_event, light_draw_demo_main,
                                &light_draw);

int main(int argc, char *argv[])
{
        light_framework_init();
        light_framework_run(argc, argv);

        return LIGHT_OK;
}

void light_draw_demo_event(const struct light_module *module, uint8_t event, void *arg)
{
        switch(event) {
                case LF_EVENT_MODULE_LOAD:
                break;
                // TODO implement unregister for event hooks
                case LF_EVENT_MODULE_UNLOAD:
                break;
        }
}
static uint8_t light_draw_demo_main(struct light_application *app)
{
#ifdef __HAVE_RP2_HW
        stdio_init_all();
#endif
        light_draw_context_t *ctx = light_draw_context_create("light_draw_demo", 128, 32, 1);
        ctx->point_radius = 2;

        light_draw_draw_point(ctx, (light_draw_point2d) {5,5});
        light_draw_draw_line(ctx, (light_draw_point2d) {8,5}, (light_draw_point2d) {14,7}, true);

        light_draw_context_set_font(ctx, &TypeLightSans_ttf_16px_font);
        light_draw_draw_text(ctx, (light_draw_point2d) {2, 14}, "LIGHT_DRAW");

        light_draw_debug_buffer_print_stdout(ctx);

        return LF_STATUS_SHUTDOWN;
}
