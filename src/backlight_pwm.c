#include <light_backlight.h>

#include "light_backlight_internal.h"

#if(LIGHT_SYSTEM == SYSTEM_PICO_SDK)
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#endif

// divides the system clock before the PWM counter. with a wrap of
// LIGHT_BACKLIGHT_LEVEL_MAX this puts the carrier around 9kHz on a 150MHz RP2350 and 8kHz on
// a 125MHz RP2040 -- far above anything the eye or a camera shutter will pick up as flicker,
// and low enough not to trouble a simple transistor or LED driver
#define BACKLIGHT_PWM_CLKDIV            16

struct backlight_pwm_state {
        uint8_t pin;
        // true when the panel's enable line SINKS current, so full brightness is a low duty
        bool active_low;
        uint8_t slice;
        uint8_t channel;
};

static struct backlight_driver_context *_pwm_spawn_context();
static void _pwm_init(struct backlight_device *dev);
static void _pwm_set_level(struct backlight_device *dev, uint16_t level);

static struct backlight_driver _driver_pwm = {
        .name = "backlight.driver:pwm",
        .spawn_context = _pwm_spawn_context,
        .init_device = _pwm_init,
        .set_level = _pwm_set_level
};

struct backlight_driver *light_backlight_driver_pwm()
{
        return &_driver_pwm;
}

static struct backlight_driver_context *_pwm_spawn_context()
{
        struct backlight_driver_context *ctx = light_alloc(sizeof(struct backlight_driver_context));
        ctx->driver = light_backlight_driver_pwm();
        ctx->state = light_alloc(sizeof(struct backlight_pwm_state));
        struct backlight_pwm_state *state = (struct backlight_pwm_state *) ctx->state;
        // light_alloc() isn't zeroed, same as every other driver state in this codebase
        state->pin = 0;
        state->active_low = false;
        state->slice = 0;
        state->channel = 0;
        return ctx;
}

static void _pwm_init(struct backlight_device *dev)
{
        struct backlight_pwm_state *state = (struct backlight_pwm_state *) dev->driver_ctx->state;
#if(LIGHT_SYSTEM == SYSTEM_PICO_SDK)
        gpio_set_function(state->pin, GPIO_FUNC_PWM);
        state->slice = (uint8_t)pwm_gpio_to_slice_num(state->pin);
        state->channel = (uint8_t)pwm_gpio_to_channel(state->pin);

        pwm_config cfg = pwm_get_default_config();
        // the integer divider variant, not pwm_config_set_clkdiv() -- that one takes a float,
        // and this tree keeps floating point out of driver paths so the same code costs the
        // same on an FPU-less RP2040
        pwm_config_set_clkdiv_int(&cfg, BACKLIGHT_PWM_CLKDIV);
        // wrap AT the level maximum, so a level maps straight onto a duty with no rescaling
        // and full brightness is a genuinely unbroken output rather than one count short
        pwm_config_set_wrap(&cfg, LIGHT_BACKLIGHT_LEVEL_MAX);
        pwm_init(state->slice, &cfg, true);
#endif
        light_info("pwm backlight '%s' on pin %d (active %s)",
                        dev->header.id, state->pin, state->active_low ? "low" : "high");
}

static void _pwm_set_level(struct backlight_device *dev, uint16_t level)
{
#if(LIGHT_SYSTEM == SYSTEM_PICO_SDK)
        struct backlight_pwm_state *state = (struct backlight_pwm_state *) dev->driver_ctx->state;
        uint16_t duty = state->active_low ? (LIGHT_BACKLIGHT_LEVEL_MAX - level) : level;
        pwm_set_chan_level(state->slice, state->channel, duty);
#else
        // host builds have no PWM hardware. tracking the level in the device struct (which
        // the core already does) is the whole of what a host build can observe, so this is a
        // no-op rather than a failure -- it keeps anything layered above testable off-target
        (void)dev;
        (void)level;
#endif
}

struct backlight_device *light_backlight_pwm_create_device(uint8_t *name, uint8_t pin, bool active_low)
{
        // pin/polarity must be attached before the device is registered: adding it to the
        // object tree (via light_backlight_init_device()) immediately triggers init_device(),
        // which configures the slice from both -- same pattern as every driver in this tree
        struct backlight_device *dev = light_object_alloc(sizeof(struct backlight_device));
        struct backlight_driver_context *driver_ctx = _pwm_spawn_context();
        struct backlight_pwm_state *state = (struct backlight_pwm_state *) driver_ctx->state;
        state->pin = pin;
        state->active_low = active_low;

        return light_backlight_init_device(dev, driver_ctx, "%s", name);
}
