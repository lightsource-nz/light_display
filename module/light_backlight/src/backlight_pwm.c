#include <light_backlight.h>
#include <light_platform.h>

#include "light_backlight_internal.h"

// divides the system clock before the PWM counter. with a wrap of
// LIGHT_BACKLIGHT_LEVEL_MAX this puts the carrier around 9kHz on a 150MHz RP2350 and 8kHz on
// a 125MHz RP2040 -- far above anything the eye or a camera shutter will pick up as flicker,
// and low enough not to trouble a simple transistor or LED driver
#define BACKLIGHT_PWM_CLKDIV            16

struct backlight_pwm_state {
        uint8_t pin;
        // true when the panel's enable line SINKS current, so full brightness is a low duty
        bool active_low;
        // the platform's PWM handle; NULL where the platform has none. slice and channel
        // bookkeeping lives behind it now rather than being tracked here
        struct lp_pwm *pwm;
};

static struct backlight_driver_context *_pwm_spawn_context();
static void _pwm_destroy_context(struct backlight_driver_context *ctx);
static void _pwm_init(struct backlight_device *dev);
static void _pwm_set_level(struct backlight_device *dev, uint16_t level);

static struct backlight_driver _driver_pwm = {
        .name = "backlight.driver:pwm",
        .spawn_context = _pwm_spawn_context,
        .destroy_context = _pwm_destroy_context,
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
        state->pwm = NULL;
        return ctx;
}

//   the counterpart to _pwm_spawn_context(), called from the device release path
// when the device this context was spawned for is freed. Frees in the reverse of
// the order allocated: the state first, then the context that points at it
static void _pwm_destroy_context(struct backlight_driver_context *ctx)
{
        struct backlight_pwm_state *state = (struct backlight_pwm_state *) ctx->state;
        //   the PWM block is a hardware resource claimed by light_platform_pwm_open(), not
        // memory: freeing the state without closing it leaves the block claimed for the rest
        // of the run, and the next open() of that pin fails.
        //
        //   the pin is driven to its INACTIVE level on the way out -- active_low is exactly
        // that level -- so teardown leaves the panel dark rather than stuck at whatever duty
        // it happened to be showing. close() on its own would leave the pin as it was
        if(state->pwm) {
                light_platform_pwm_release_pin(state->pwm, state->active_low);
                light_platform_pwm_close(state->pwm);
        }
        light_free((void *)ctx->state);
        light_free(ctx);
}

static void _pwm_init(struct backlight_device *dev)
{
        struct backlight_pwm_state *state = (struct backlight_pwm_state *) dev->driver_ctx->state;
        state->pwm = light_platform_pwm_open(state->pin);
        // NULL is the normal answer on a platform with no PWM, not a failure. every call
        // below tolerates it, and the level the core tracks in the device struct is the whole
        // of what such a build could observe anyway -- which keeps everything layered above
        // this testable off-target
        if(state->pwm) {
                // wrap AT the level maximum, so a level maps straight onto a duty with no
                // rescaling and full brightness is a genuinely unbroken output rather than
                // one count short
                light_platform_pwm_configure(state->pwm, LIGHT_BACKLIGHT_LEVEL_MAX,
                                BACKLIGHT_PWM_CLKDIV);
        }
        light_info("pwm backlight '%s' on pin %d (active %s, group %d)",
                        dev->header.id, state->pin, state->active_low ? "low" : "high",
                        light_platform_pwm_get_group(state->pwm));
}

static void _pwm_set_level(struct backlight_device *dev, uint16_t level)
{
        struct backlight_pwm_state *state = (struct backlight_pwm_state *) dev->driver_ctx->state;
        uint16_t duty = state->active_low ? (LIGHT_BACKLIGHT_LEVEL_MAX - level) : level;
        light_platform_pwm_set_duty(state->pwm, duty);
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
