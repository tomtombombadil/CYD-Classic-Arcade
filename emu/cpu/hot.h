// EMU_HOT marks the CPU interpreters' hot functions. On the board
// (components/arcade_core defines EMU_HOT_IRAM) they go into the ESP32's
// internal instruction RAM: its 32 KB flash cache can't hold an
// interpreter's dispatch, and every miss stalls. Elsewhere it's empty.
#pragma once

#if defined(EMU_HOT_IRAM) && !defined(EMU_HOT)
#include <esp_attr.h>
#define EMU_HOT IRAM_ATTR
#endif
#ifndef EMU_HOT
#define EMU_HOT
#endif
