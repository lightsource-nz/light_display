#ifndef _LIGHT_DISPLAY_INTERNAL_H
#define _LIGHT_DISPLAY_INTERNAL_H

// called once per scheduler tick by light_display's own periodic task (module.c) to drive
// every live device's in-flight async update forward. not meant to be called by application
// code, hence kept out of the public header
extern void light_display_poll_async_updates(void);

#endif
