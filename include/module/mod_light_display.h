#ifndef _MOD_LIGHT_DISPLAY_H
#define _MOD_LIGHT_DISPLAY_H

#include <light.h>

#define DISPLAY_BUFFER_SINGLE               1
#define DISPLAY_BUFFER_DOUBLE               2
// TODO I think triple buffering is a thing but idk why or how

#ifndef CFG_LIGHT_DISPLAY_BUFFERING
#define CFG_LIGHT_DISPLAY_BUFFERING         DISPLAY_BUFFER_SINGLE
#endif

// TODO implement version fields properly
#define LIGHT_DISPLAY_VERSION_STR           "0.1.0"

#define LIGHT_DISPLAY_INFO_STR              "light_display v" LIGHT_DISPLAY_VERSION_STR

Light_Module_Declare(light_display)

#endif