// The game list, shared by the firmware and the PC tools (MIT).
// Each entry: the machine behind a common face, its ROM set, its screen,
// and how a game controller's buttons map onto the cabinet's panel.
// Adding a game = its machine in emu/machines/ + one entry in games.cpp.
#pragma once

#include "rom/romset.h"

#include <cstdint>

namespace samples { struct Wav; }

namespace games {

// Controller buttons, the same bits as the firmware's PAD_* (main/pad.h).
// Face buttons by position: A = bottom, B = right, X = left, Y = top.
enum Pad : uint32_t {
    kPadUp = 1u << 0, kPadDown = 1u << 1, kPadLeft = 1u << 2, kPadRight = 1u << 3,
    kPadA = 1u << 4, kPadB = 1u << 5, kPadX = 1u << 6, kPadY = 1u << 7,
    kPadL = 1u << 8, kPadR = 1u << 9,
    kPadStart = 1u << 10, kPadSelect = 1u << 11, kPadHome = 1u << 12,
    // The right stick pushed past a third (twin-stick games: Robotron)
    kPadRUp = 1u << 13, kPadRDown = 1u << 14, kPadRLeft = 1u << 15, kPadRRight = 1u << 16,
};

// Both sticks as numbers, -512..511 (down / right positive); 0 = centred
// or no controller. For spinners, trackballs and analog sticks.
struct Analog {
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

// One emulated machine. Regions are filled by romset::load() in the order
// of the set's regions; they're kept by pointer (RAM or flash).
class Machine {
public:
    virtual ~Machine() = default;
    // `swap_rgb565_bytes`: pixels byte-swapped, as SPI LCDs take them.
    virtual void init(const uint8_t* const* regions, uint32_t audio_rate, bool swap_rgb565_bytes) = 0;
    virtual void set_dips(uint8_t dips) = 0;
    virtual void set_inputs(uint16_t buttons) = 0;
    // The sticks, every frame (before run_frame); games with only buttons
    // ignore it.
    virtual void set_analog(const Analog&) {}
    // One frame: emulates it, writes its audio (signed 16-bit mono, up to
    // `cap` samples) and returns the sample count.
    virtual int  run_frame(int16_t* audio, int cap) = 0;
    // Line y of the last frame, `Info::width` RGB565 pixels.
    virtual void render_line(int y, uint16_t* out) = 0;
    virtual int  watchdog_resets() const = 0;
    // PC tool only: MAME sample recordings, indexed like Info::sample_names
    // (null = the sound model). The firmware never uses samples (Tom).
    virtual void set_samples(const samples::Wav* const*) {}
};

enum class Orientation : uint8_t { Portrait, Landscape };

// A controller button -> the cabinet buttons it presses.
struct Control {
    uint32_t pad;
    uint16_t machine;
};

// A cabinet button by name, for the PC tool's input scripts.
struct ButtonName {
    const char* name;
    uint16_t    bit;
};

struct Info {
    const char*        id;              // = the MAME set name, e.g. "pacman"
    const char*        title;           // "Pac-Man"
    const romset::Set* set;
    int                width, height;   // as the player sees it
    Orientation        orientation;
    uint32_t           cpu_clock;       // Hz, with cycles_per_frame: the frame rate
    int                cycles_per_frame;
    const Control*     controls;
    int                control_count;
    const ButtonName*  buttons;
    int                button_count;
    const char* const* sample_names;    // PC tool: the MAME sample set's files (no .wav), or null
    int                sample_count;
    Machine* (*make)();                 // new machine (delete when done)
    // The cabinet had a 4-way joystick (Pac-Man): never two directions at
    // once - a d-pad's diagonal counts as the direction pressed last.
    bool               four_way;
};

extern const Info kGames[];
extern const int  kGameCount;

// The entry for a set name, or null.
const Info* find(const char* id);

// The cabinet buttons pressed by these controller buttons.
uint16_t map_pad(const Info& game, uint32_t pad);

// Makes a d-pad act like a 4-way joystick: of the directions held, only
// the one pressed last (see Info::four_way). Keep one per player.
class FourWay {
public:
    uint32_t filter(uint32_t pad);
private:
    uint32_t prev_ = 0, last_ = 0;
};

// Frames per second, e.g. 60.606 for Pac-Man.
inline double frame_rate(const Info& g) { return double(g.cpu_clock) / g.cycles_per_frame; }

} // namespace games
