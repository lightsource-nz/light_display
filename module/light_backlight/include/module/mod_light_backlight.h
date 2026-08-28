#ifndef _MOD_LIGHT_BACKLIGHT_H
#define _MOD_LIGHT_BACKLIGHT_H

#include <light.h>

// the repository's version, derived from its git tags -- see light_project_version(LIGHT_DISPLAY)
#include <light_display_version.h>
#define LIGHT_BACKLIGHT_VERSION_STR       LIGHT_DISPLAY_VERSION_STRING

#define LIGHT_BACKLIGHT_INFO_STR          "light_backlight v" LIGHT_BACKLIGHT_VERSION_STR

Light_Module_Declare(light_backlight);

#endif
