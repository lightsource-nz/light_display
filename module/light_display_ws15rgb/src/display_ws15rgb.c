#include <light_display_ws15rgb.h>

//   The whole of this module is one call, and that is the point rather than an oversight.
// Everything else about the Waveshare 1.5inch RGB OLED Module is a constant in the header,
// because the product IS its geometry -- the controller behaviour is in
// light_display_ssd1351 and the wiring is in whichever rig module owns the pins.
//
//   Compare light_display_po13, which also carries setup_io_*() helpers: that panel is an
// expansion board with a fixed header, so its pins are a property of the product. This one
// has flying leads, so they are not.

struct display_device *light_display_ws15rgb_create_device(uint8_t *name, struct io_context *io)
{
        return light_display_ssd1351_create_device(name, WS15RGB_WIDTH, WS15RGB_HEIGHT, io);
}
