#ifndef _LIGHT_BACKLIGHT_INTERNAL_H
#define _LIGHT_BACKLIGHT_INTERNAL_H

// called once per scheduler tick by light_backlight's own periodic task (module.c) to
// advance any fade in progress. not meant to be called by application code, hence kept out
// of the public header
extern void light_backlight_poll_devices(void);

#endif
