// The game list (see games.h).
#include "games.h"

#include "machines/digdug.h"
#include "machines/dkong.h"
#include "machines/galaga.h"
#include "machines/invaders.h"
#include "machines/mrdo.h"
#include "machines/mspacman.h"
#include "machines/pacman.h"
#include "machines/rallyx.h"

#include <cstring>

namespace games {
namespace {

// ---- Pac-Man ----------------------------------------------------------------

class Pacman : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init({r[pacman::kRegionCpu], r[pacman::kRegionGfx], r[pacman::kRegionProms],
                 r[pacman::kRegionSound]}, rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    pacman::Machine m_;
};

// Select = coin, Start = 1 player, L = 2 players (they take turns).
const Control kPacmanControls[] = {
    {kPadUp, pacman::kUp}, {kPadDown, pacman::kDown}, {kPadLeft, pacman::kLeft},
    {kPadRight, pacman::kRight}, {kPadSelect, pacman::kCoin}, {kPadStart, pacman::kStart1},
    {kPadL, pacman::kStart2},
};

const ButtonName kPacmanButtons[] = {
    {"up", pacman::kUp}, {"down", pacman::kDown}, {"left", pacman::kLeft},
    {"right", pacman::kRight}, {"coin", pacman::kCoin}, {"coin2", pacman::kCoin2},
    {"start1", pacman::kStart1}, {"start2", pacman::kStart2},
    {"service", pacman::kService}, {"test", pacman::kTest},
};

// ---- Ms. Pac-Man (the Pac-Man board + Midway's aux board) -------------------

class Mspacman : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init({r[mspacman::kRegionCpu], r[mspacman::kRegionGfx], r[mspacman::kRegionProms],
                 r[mspacman::kRegionSound]}, r[mspacman::kRegionAux], rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    mspacman::Machine m_;
};

// ---- Space Invaders ---------------------------------------------------------

class Invaders : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init({r[invaders::kRegionCpu]}, rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }
    void set_samples(const samples::Wav* const* w) override { m_.set_samples(w); }

private:
    invaders::Machine m_;
};

// The upright cabinet's players share one panel: either controller moves
// and fires. A or B fires.
const Control kInvadersControls[] = {
    {kPadLeft, invaders::kLeft}, {kPadRight, invaders::kRight}, {kPadA, invaders::kFire},
    {kPadB, invaders::kFire}, {kPadSelect, invaders::kCoin}, {kPadStart, invaders::kStart1},
    {kPadL, invaders::kStart2},
};

const ButtonName kInvadersButtons[] = {
    {"coin", invaders::kCoin}, {"start1", invaders::kStart1}, {"start2", invaders::kStart2},
    {"left", invaders::kLeft}, {"right", invaders::kRight}, {"fire", invaders::kFire},
    {"left2", invaders::kLeft2}, {"right2", invaders::kRight2}, {"fire2", invaders::kFire2},
};

// ---- Rally-X ----------------------------------------------------------------

class Rallyx : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init({r[rallyx::kRegionCpu], r[rallyx::kRegionGfx], r[rallyx::kRegionDots],
                 r[rallyx::kRegionProms], r[rallyx::kRegionSound]}, rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    rallyx::Machine m_;
};

// A = smoke screen, Select = coin, Start = 1 player, L = 2 players (they
// take turns).
const Control kRallyxControls[] = {
    {kPadUp, rallyx::kUp}, {kPadDown, rallyx::kDown}, {kPadLeft, rallyx::kLeft},
    {kPadRight, rallyx::kRight}, {kPadA, rallyx::kSmoke}, {kPadSelect, rallyx::kCoin},
    {kPadStart, rallyx::kStart1}, {kPadL, rallyx::kStart2},
};

const ButtonName kRallyxButtons[] = {
    {"up", rallyx::kUp}, {"down", rallyx::kDown}, {"left", rallyx::kLeft},
    {"right", rallyx::kRight}, {"smoke", rallyx::kSmoke}, {"coin", rallyx::kCoin},
    {"coin2", rallyx::kCoin2}, {"start1", rallyx::kStart1}, {"start2", rallyx::kStart2},
    {"service", rallyx::kService},
};

// ---- Donkey Kong ------------------------------------------------------------

class Dkong : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init({r[dkong::kRegionCpu], r[dkong::kRegionSound], r[dkong::kRegionTiles],
                 r[dkong::kRegionSprites], r[dkong::kRegionProms]}, rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    dkong::Machine m_;
};

// The cabinet's 4-way stick and its one button: A (or B) jumps.
const Control kDkongControls[] = {
    {kPadUp, dkong::kUp}, {kPadDown, dkong::kDown}, {kPadLeft, dkong::kLeft},
    {kPadRight, dkong::kRight}, {kPadA, dkong::kJump}, {kPadB, dkong::kJump},
    {kPadSelect, dkong::kCoin}, {kPadStart, dkong::kStart1}, {kPadL, dkong::kStart2},
};

const ButtonName kDkongButtons[] = {
    {"up", dkong::kUp}, {"down", dkong::kDown}, {"left", dkong::kLeft}, {"right", dkong::kRight},
    {"jump", dkong::kJump}, {"coin", dkong::kCoin}, {"start1", dkong::kStart1}, {"start2", dkong::kStart2},
};

// ---- Galaga ---------------------------------------------------------------

class Galaga : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init({r[galaga::kRegionMain], r[galaga::kRegionSub], r[galaga::kRegionSound], r[galaga::kRegionChars],
                 r[galaga::kRegionSprites], r[galaga::kRegionProms], r[galaga::kRegionWave]},
                rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    galaga::Machine m_;
};

// ---- Dig Dug --------------------------------------------------------------

class Digdug : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init({r[digdug::kRegionMain], r[digdug::kRegionSub], r[digdug::kRegionSound], r[digdug::kRegionText],
                 r[digdug::kRegionSprites], r[digdug::kRegionTiles], r[digdug::kRegionMap], r[digdug::kRegionProms],
                 r[digdug::kRegionWave]},
                rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    digdug::Machine m_;
};

// The Namco boards share one panel layout (namco_galaga::Button). A or B
// fires (Galaga) or pumps (Dig Dug); Galaga's stick moves left and right
// only (up / down do nothing there).
const Control kNamcoControls[] = {
    {kPadUp, namco_galaga::kUp}, {kPadDown, namco_galaga::kDown}, {kPadLeft, namco_galaga::kLeft},
    {kPadRight, namco_galaga::kRight}, {kPadA, namco_galaga::kFire}, {kPadB, namco_galaga::kFire},
    {kPadSelect, namco_galaga::kCoin}, {kPadStart, namco_galaga::kStart1}, {kPadL, namco_galaga::kStart2},
};

const ButtonName kNamcoButtons[] = {
    {"up", namco_galaga::kUp}, {"down", namco_galaga::kDown}, {"left", namco_galaga::kLeft},
    {"right", namco_galaga::kRight}, {"fire", namco_galaga::kFire}, {"coin", namco_galaga::kCoin},
    {"coin2", namco_galaga::kCoin2}, {"start1", namco_galaga::kStart1}, {"start2", namco_galaga::kStart2},
    {"service", namco_galaga::kService}, {"test", namco_galaga::kTest},
};

// ---- Mr. Do! ----------------------------------------------------------------

class Mrdo : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init({r[mrdo::kRegionCpu], r[mrdo::kRegionFg], r[mrdo::kRegionBg], r[mrdo::kRegionSprites],
                 r[mrdo::kRegionProms]}, rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    mrdo::Machine m_;
};

// The 4-way stick and one button: A (or B) throws the power ball.
const Control kMrdoControls[] = {
    {kPadUp, mrdo::kUp}, {kPadDown, mrdo::kDown}, {kPadLeft, mrdo::kLeft}, {kPadRight, mrdo::kRight},
    {kPadA, mrdo::kFire}, {kPadB, mrdo::kFire}, {kPadSelect, mrdo::kCoin}, {kPadStart, mrdo::kStart1},
    {kPadL, mrdo::kStart2},
};

const ButtonName kMrdoButtons[] = {
    {"up", mrdo::kUp}, {"down", mrdo::kDown}, {"left", mrdo::kLeft}, {"right", mrdo::kRight},
    {"fire", mrdo::kFire}, {"coin", mrdo::kCoin}, {"coin2", mrdo::kCoin2}, {"start1", mrdo::kStart1},
    {"start2", mrdo::kStart2}, {"tilt", mrdo::kTilt},
};

template <class M> Machine* make() { return new M; }

#define COUNT(a) int(sizeof(a) / sizeof((a)[0]))

} // namespace

const Info kGames[] = {
    {"pacman", "Pac-Man", &pacman::kRomSet, pacman::kScreenW, pacman::kScreenH, Orientation::Portrait,
     pacman::kCpuClock, pacman::kCyclesPerFrame, kPacmanControls, COUNT(kPacmanControls),
     kPacmanButtons, COUNT(kPacmanButtons), nullptr, 0, make<Pacman>, true},
    {"invaders", "Space Invaders", &invaders::kRomSet, invaders::kScreenW, invaders::kScreenH,
     Orientation::Portrait, invaders::kCpuClock, invaders::kCyclesPerFrame, kInvadersControls,
     COUNT(kInvadersControls), kInvadersButtons, COUNT(kInvadersButtons), invaders::kSampleNames,
     invaders::kSampleCount, make<Invaders>, false},
    {"rallyx", "Rally-X", &rallyx::kRomSet, rallyx::kScreenW, rallyx::kScreenH, Orientation::Landscape,
     rallyx::kCpuClock, rallyx::kCyclesPerFrame, kRallyxControls, COUNT(kRallyxControls),
     kRallyxButtons, COUNT(kRallyxButtons), nullptr, 0, make<Rallyx>, true},
    {"dkong", "Donkey Kong", &dkong::kRomSet, dkong::kScreenW, dkong::kScreenH, Orientation::Portrait,
     dkong::kCpuClock, dkong::kCyclesPerFrame, kDkongControls, COUNT(kDkongControls), kDkongButtons,
     COUNT(kDkongButtons), nullptr, 0, make<Dkong>, true},
    {"galaga", "Galaga", &galaga::kRomSet, galaga::kScreenW, galaga::kScreenH, Orientation::Portrait,
     galaga::kCpuClock, galaga::kCyclesPerFrame, kNamcoControls, COUNT(kNamcoControls), kNamcoButtons,
     COUNT(kNamcoButtons), nullptr, 0, make<Galaga>, false},
    {"digdug", "Dig Dug", &digdug::kRomSet, digdug::kScreenW, digdug::kScreenH, Orientation::Portrait,
     digdug::kCpuClock, digdug::kCyclesPerFrame, kNamcoControls, COUNT(kNamcoControls), kNamcoButtons,
     COUNT(kNamcoButtons), nullptr, 0, make<Digdug>, true},
    {"mspacman", "Ms. Pac-Man", &mspacman::kRomSet, pacman::kScreenW, pacman::kScreenH, Orientation::Portrait,
     pacman::kCpuClock, pacman::kCyclesPerFrame, kPacmanControls, COUNT(kPacmanControls),
     kPacmanButtons, COUNT(kPacmanButtons), nullptr, 0, make<Mspacman>, true},
    {"mrdo", "Mr. Do!", &mrdo::kRomSet, mrdo::kScreenW, mrdo::kScreenH, Orientation::Portrait,
     mrdo::kCpuClock, mrdo::kCyclesPerFrame, kMrdoControls, COUNT(kMrdoControls), kMrdoButtons,
     COUNT(kMrdoButtons), nullptr, 0, make<Mrdo>, true},
};

const int kGameCount = COUNT(kGames);

const Info* find(const char* id)
{
    for (int i = 0; i < kGameCount; ++i)
        if (std::strcmp(kGames[i].id, id) == 0) return &kGames[i];
    return nullptr;
}

uint32_t FourWay::filter(uint32_t pad)
{
    constexpr uint32_t kDirs = kPadUp | kPadDown | kPadLeft | kPadRight;
    const uint32_t dirs = pad & kDirs;
    const uint32_t fresh = dirs & ~prev_;
    prev_ = dirs;
    if (fresh) last_ = fresh & (0u - fresh);        // newest press (lowest bit if several)
    uint32_t keep = dirs & last_;
    if (!keep && dirs) keep = dirs & (0u - dirs);   // the newest was let go: one still held
    if (!dirs) last_ = 0;
    return (pad & ~kDirs) | keep;
}

uint16_t map_pad(const Info& game, uint32_t pad)
{
    uint16_t b = 0;
    for (int i = 0; i < game.control_count; ++i)
        if (pad & game.controls[i].pad) b |= game.controls[i].machine;
    return b;
}

} // namespace games
