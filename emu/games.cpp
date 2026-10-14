// The game list (see games.h).
#include "games.h"

#include "machines/digdug.h"
#include "machines/galaga.h"
#include "machines/invaders.h"
#include "machines/pacman.h"

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
    {"galaga", "Galaga", &galaga::kRomSet, galaga::kScreenW, galaga::kScreenH, Orientation::Portrait,
     galaga::kCpuClock, galaga::kCyclesPerFrame, kNamcoControls, COUNT(kNamcoControls), kNamcoButtons,
     COUNT(kNamcoButtons), nullptr, 0, make<Galaga>, false},
    {"digdug", "Dig Dug", &digdug::kRomSet, digdug::kScreenW, digdug::kScreenH, Orientation::Portrait,
     digdug::kCpuClock, digdug::kCyclesPerFrame, kNamcoControls, COUNT(kNamcoControls), kNamcoButtons,
     COUNT(kNamcoButtons), nullptr, 0, make<Digdug>, true},
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
