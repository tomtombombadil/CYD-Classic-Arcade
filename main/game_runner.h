// Plays one game until the player goes back (Start + Select held, or Home).
#pragma once

#include "audio_ring.h"

namespace game_runner {

// `game`: index into games::kGames. `ring`: the audio output's ring.
void run(int game, AudioRing& ring);

// The last game's speed, measured by the clock over the whole time it ran
// (shown in the menu afterwards: is a game too fast or slow on a board?)
// (shown in the menu afterwards and on the About page: where a board's
// time goes - the emulation, building the picture's lines, or sending them)
struct Stats {
    float frames_per_s = 0;   // emulated frames (the game's speed; ~60)
    float drawn_per_s = 0;    // pictures sent to the screen
    float game_hz = 0;        // the game's own frame rate
    float emu_ms = 0, emu_max_ms = 0;          // a frame's emulation
    float draw_ms = 0, draw_max_ms = 0;        // a picture: render + hash + send
    float send_ms = 0;                         // a picture's part spent sending
    float kpixels = 0;                         // pixels sent a picture, thousands
    int worst_frames = 0, worst_drawn = 0;     // in the slowest second
};
const Stats& last_stats();

}  // namespace game_runner
