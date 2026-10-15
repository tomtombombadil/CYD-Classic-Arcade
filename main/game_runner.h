// Plays one game until the player goes back (Start + Select held, or Home).
#pragma once

#include "audio_ring.h"

namespace game_runner {

// `game`: index into games::kGames. `ring`: the audio output's ring.
void run(int game, AudioRing& ring);

// The last game's speed, measured by the clock over the whole time it ran
// (shown in the menu afterwards: is a game too fast or slow on a board?)
struct Stats {
    float frames_per_s = 0;   // emulated frames (the game's speed; ~60)
    float drawn_per_s = 0;    // pictures sent to the screen
    float emu_ms = 0, draw_ms = 0;
};
const Stats& last_stats();

}  // namespace game_runner
