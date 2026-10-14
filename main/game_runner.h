// Plays one game until the player goes back (Start + Select held, or Home).
#pragma once

#include "audio_ring.h"

namespace game_runner {

// `game`: index into games::kGames. `ring`: the audio output's ring.
void run(int game, AudioRing& ring);

}  // namespace game_runner
