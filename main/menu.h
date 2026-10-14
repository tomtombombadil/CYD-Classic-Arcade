// The start menu: the games (playable ones bright, the rest "no ROMs"),
// Controller Test, and the settings. Driven by the controller: d-pad to
// move, A or Start to choose, left / right to change a setting.
#pragma once

#include "audio_ring.h"

namespace menu {

// Never returns.
void run(AudioRing& ring);

}  // namespace menu
