// The game list (see games.h).
#include "games.h"

#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "machines/asteroids.h"
#include "machines/btime.h"
#include "machines/digdug.h"
#include "machines/dkong.h"
#include "machines/galaga.h"
#include "machines/invaders.h"
#include "machines/missile.h"
#include "machines/mrdo.h"
#include "machines/mspacman.h"
#include "machines/pacman.h"
#include "machines/qbert.h"
#include "machines/qix.h"
#include "machines/rallyx.h"
#include "machines/starwars.h"
#include "machines/tempest.h"
#include "machines/williams.h"
#include "machines/bzone.h"
#include "machines/zaxxon.h"
#include "machines/amidar.h"
#include "machines/galaxian.h"
#include "machines/mappy.h"

#include <cstring>
#include <new>

namespace games {

namespace {

// A copy of a tile machine's VideoState for drawing on another core
// (Machine::snapshot); allocated at the first snapshot, ~2-5 KB. The
// machines' render_line(vs, ...) read only the copy and tables made at
// init.
template <class M> class Shot {
public:
    using VS = std::remove_cv_t<std::remove_reference_t<decltype(std::declval<const M&>().video())>>;
    Shot() = default;
    Shot(const Shot&) = delete;
    Shot& operator=(const Shot&) = delete;
    ~Shot()
    {
        if (vs_) vs_->~VS();
        emu::free(vs_);
    }
    bool take(const M& m)
    {
        if (!vs_) {
            void* p = emu::alloc(sizeof(VS));
            if (!p) return false;
            vs_ = new (p) VS;
        }
        *vs_ = m.video();
        return true;
    }
    void render(const M& m, int y, uint16_t* out) const { m.render_line(*vs_, y, out); }

private:
    VS* vs_ = nullptr;
};

}  // namespace
namespace {

// Demo sounds (Machine::set_demo_sounds): only Galaga, Dig Dug, Q*bert,
// Qix and Star Wars have the switch (a DIP switch, or Qix's operator
// setting); the others' attract modes are silent on the cabinet, with no
// switch for it (MAME's DIP lists, Robotron's and Defender's
// operator pages), so they keep the default (false).

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
    bool snapshot() override { return shot_.take(m_); }
    void render_snapshot_line(int y, uint16_t* out) override { shot_.render(m_, y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    pacman::Machine m_;
    Shot<pacman::Machine> shot_;
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
    bool snapshot() override { return shot_.take(m_); }
    void render_snapshot_line(int y, uint16_t* out) override { shot_.render(m_, y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    mspacman::Machine m_;
    Shot<mspacman::Machine> shot_;
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
    bool snapshot() override { return shot_.take(m_); }
    void render_snapshot_line(int y, uint16_t* out) override { shot_.render(m_, y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    rallyx::Machine m_;
    Shot<rallyx::Machine> shot_;
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
    bool snapshot() override { return shot_.take(m_); }
    void render_snapshot_line(int y, uint16_t* out) override { shot_.render(m_, y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    dkong::Machine m_;
    Shot<dkong::Machine> shot_;
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
    // DIP bank A's Demo Sounds (SWB:4): the demo play's shots, explosions
    // and tunes. On by default.
    bool set_demo_sounds(bool on) override { m_.set_demo_sounds(on); return true; }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    bool snapshot() override { return shot_.take(m_); }
    void render_snapshot_line(int y, uint16_t* out) override { shot_.render(m_, y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    galaga::Machine m_;
    Shot<galaga::Machine> shot_;
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
    // DIP bank B's Demo Sounds (SWB:4): the demo play's sounds. On by
    // default.
    bool set_demo_sounds(bool on) override { m_.set_demo_sounds(on); return true; }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    bool snapshot() override { return shot_.take(m_); }
    void render_snapshot_line(int y, uint16_t* out) override { shot_.render(m_, y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    digdug::Machine m_;
    Shot<digdug::Machine> shot_;
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

// ---- Burger Time ------------------------------------------------------------

class Btime : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init({r[btime::kRegionCpu], r[btime::kRegionSound], r[btime::kRegionGfx], r[btime::kRegionTiles],
                 r[btime::kRegionMap]},
                rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    btime::Machine m_;
};

// The cabinet's 4-way stick and its one button: A (or B) shakes pepper.
const Control kBtimeControls[] = {
    {kPadUp, btime::kUp}, {kPadDown, btime::kDown}, {kPadLeft, btime::kLeft},
    {kPadRight, btime::kRight}, {kPadA, btime::kPepper}, {kPadB, btime::kPepper},
    {kPadSelect, btime::kCoin}, {kPadStart, btime::kStart1}, {kPadL, btime::kStart2},
};

const ButtonName kBtimeButtons[] = {
    {"up", btime::kUp}, {"down", btime::kDown}, {"left", btime::kLeft}, {"right", btime::kRight},
    {"pepper", btime::kPepper}, {"coin", btime::kCoin}, {"coin2", btime::kCoin2},
    {"start1", btime::kStart1}, {"start2", btime::kStart2},
};

// ---- Q*bert ---------------------------------------------------------------

class Qbert : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init({r[qbert::kRegionMain], r[qbert::kRegionSound], r[qbert::kRegionTiles],
                 r[qbert::kRegionSprites]}, rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    // The Demo Sounds switch (DSW 2): the demo's hops, speech and music.
    // On by default.
    bool set_demo_sounds(bool on) override { m_.set_demo_sounds(on); return true; }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    bool snapshot() override { return shot_.take(m_); }
    void render_snapshot_line(int y, uint16_t* out) override { shot_.render(m_, y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    qbert::Machine m_;
    Shot<qbert::Machine> shot_;
};

// The stick is turned 45 degrees (Info::diagonal): up = up-right, right =
// down-right, down = down-left, left = up-left, and held diagonals count
// as themselves. No buttons in the game: Select = coin, Start = 1 player,
// L = 2 players (they take turns).
const Control kQbertControls[] = {
    {kPadUp, qbert::kUpRight}, {kPadRight, qbert::kDownRight}, {kPadDown, qbert::kDownLeft},
    {kPadLeft, qbert::kUpLeft}, {kPadSelect, qbert::kCoin}, {kPadStart, qbert::kStart1},
    {kPadL, qbert::kStart2},
};

const ButtonName kQbertButtons[] = {
    {"upright", qbert::kUpRight}, {"downright", qbert::kDownRight}, {"downleft", qbert::kDownLeft},
    {"upleft", qbert::kUpLeft}, {"coin", qbert::kCoin}, {"coin2", qbert::kCoin2},
    {"start1", qbert::kStart1}, {"start2", qbert::kStart2}, {"test", qbert::kTest},
    {"select", qbert::kSelect},
};

// ---- Asteroids ------------------------------------------------------------

class Asteroids : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init({r[asteroids::kRegionCpu], r[asteroids::kRegionVector]}, rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }
    bool snapshot() override { return m_.snapshot(); }
    void render_snapshot_line(int y, uint16_t* out) override { m_.render_snapshot_line(y, out); }

private:
    asteroids::Machine m_;
};

// Left / right rotate, A fires, B (or up) thrusts, X hyperspace.
const Control kAsteroidsControls[] = {
    {kPadLeft, asteroids::kLeft}, {kPadRight, asteroids::kRight}, {kPadA, asteroids::kFire},
    {kPadB, asteroids::kThrust}, {kPadUp, asteroids::kThrust}, {kPadX, asteroids::kHyperspace},
    {kPadSelect, asteroids::kCoin}, {kPadStart, asteroids::kStart1}, {kPadL, asteroids::kStart2},
};

const ButtonName kAsteroidsButtons[] = {
    {"left", asteroids::kLeft}, {"right", asteroids::kRight}, {"thrust", asteroids::kThrust},
    {"fire", asteroids::kFire}, {"hyperspace", asteroids::kHyperspace}, {"coin", asteroids::kCoin},
    {"coin2", asteroids::kCoin2}, {"start1", asteroids::kStart1}, {"start2", asteroids::kStart2},
    {"test", asteroids::kTest}, {"step", asteroids::kDiagStep},
};

// ---- Tempest ----------------------------------------------------------------

class Tempest : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init({r[tempest::kRegionCpu], r[tempest::kRegionVector]}, rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    // The left stick's X turns the spinner at a speed in proportion: full
    // over = kSpinnerFull / 256 counts a frame (the d-pad: a steady
    // tempest::Machine::kButtonSpin); right = counter-clockwise, as the d-pad.
    void set_analog(const Analog& a) override { m_.set_spinner(-a.lx * kSpinnerFull / 512); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }
    bool snapshot() override { return m_.snapshot(); }
    void render_snapshot_line(int y, uint16_t* out) override { m_.render_snapshot_line(y, out); }

    static constexpr int kSpinnerFull = 1024;      // 4 counts a frame

private:
    tempest::Machine m_;
};

// D-pad left / right move the player round the tube at a steady speed, the
// left stick at a speed in proportion: right = counter-clockwise on the
// screen (Tom, v0.14.4: the player usually sits at the bottom, where
// counter-clockwise is to the right); A fires, B is the Superzapper.
const Control kTempestControls[] = {
    {kPadLeft, tempest::kSpinRight}, {kPadRight, tempest::kSpinLeft}, {kPadA, tempest::kFire},
    {kPadB, tempest::kZap}, {kPadSelect, tempest::kCoin}, {kPadStart, tempest::kStart1},
    {kPadL, tempest::kStart2},
};

const ButtonName kTempestButtons[] = {
    {"fire", tempest::kFire}, {"zap", tempest::kZap}, {"left", tempest::kSpinLeft},
    {"right", tempest::kSpinRight}, {"coin", tempest::kCoin}, {"coin2", tempest::kCoin2},
    {"start1", tempest::kStart1}, {"start2", tempest::kStart2}, {"test", tempest::kTest},
    {"step", tempest::kDiagStep},
};

// ---- Missile Command ----------------------------------------------------------

class Missile : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init(r[missile::kRegionCpu], rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    // The left stick rolls the trackball at a speed in proportion: full
    // over = kTrackFull / 256 counts a frame (the d-pad: 1 count a frame,
    // rising while held - missile::Machine::button_speed); up = up on the screen.
    void set_analog(const Analog& a) override
    {
        m_.set_trackball(a.lx * kTrackFull / 512, -a.ly * kTrackFull / 512);
    }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }
    bool snapshot() override { return m_.snapshot(); }
    void render_snapshot_line(int y, uint16_t* out) override { m_.render_snapshot_line(y, out); }

    static constexpr int kTrackFull = 2048;     // 8 counts (pixels) a frame

private:
    missile::Machine m_;
};

// D-pad: the crosshair, faster the longer it's held; the left stick in proportion; the
// three bases' fire buttons by position: X = left, A = centre, B = right.
const Control kMissileControls[] = {
    {kPadUp, missile::kUp}, {kPadDown, missile::kDown}, {kPadLeft, missile::kLeft},
    {kPadRight, missile::kRight}, {kPadX, missile::kFireLeft}, {kPadA, missile::kFireCentre},
    {kPadB, missile::kFireRight}, {kPadSelect, missile::kCoin}, {kPadStart, missile::kStart1},
    {kPadL, missile::kStart2},
};

const ButtonName kMissileButtons[] = {
    {"up", missile::kUp}, {"down", missile::kDown}, {"left", missile::kLeft}, {"right", missile::kRight},
    {"fireleft", missile::kFireLeft}, {"fire", missile::kFireCentre}, {"fireright", missile::kFireRight},
    {"coin", missile::kCoin}, {"coin2", missile::kCoin2}, {"start1", missile::kStart1},
    {"start2", missile::kStart2}, {"test", missile::kTest}, {"tilt", missile::kTilt},
};

// ---- Battlezone ---------------------------------------------------------------

class Bzone : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init({r[bzone::kRegionCpu], r[bzone::kRegionVector]}, rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { buttons_ = b; }
    // The sticks' Y drive the treads, tank style (bzone::treads()).
    void set_analog(const Analog& a) override { ly_ = a.ly; ry_ = a.ry; }
    int  run_frame(int16_t* a, int cap) override
    {
        m_.set_inputs(bzone::treads(buttons_, ly_, ry_));
        return m_.run_frame(a, cap);
    }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }
    bool snapshot() override { return m_.snapshot(); }
    void render_snapshot_line(int y, uint16_t* out) override { m_.render_snapshot_line(y, out); }

private:
    bzone::Machine m_;
    uint16_t buttons_ = 0;
    int      ly_ = 0, ry_ = 0;
};

// The d-pad drives: up = both treads forward, down = both back, left = left
// back + right forward (turns left on the spot), right the mirror; a
// diagonal adds both, and a tread pressed both ways stops (bzone::Machine::
// in3), so up + left = right tread only (forward, turning left), down +
// left = left tread back. Or the sticks, tank style: left stick Y = left
// tread, right stick Y = right tread (bzone::treads). A, B and the
// shoulder buttons fire (Tom, 2026-10-20: with both thumbs on the sticks
// only L1 / L2 / R1 / R2 can be reached). A one-player game: no 2P start.
const Control kBzoneControls[] = {
    {kPadUp, bzone::kLeftFwd | bzone::kRightFwd}, {kPadDown, bzone::kLeftBack | bzone::kRightBack},
    {kPadLeft, bzone::kLeftBack | bzone::kRightFwd}, {kPadRight, bzone::kLeftFwd | bzone::kRightBack},
    {kPadA, bzone::kFire}, {kPadB, bzone::kFire}, {kPadL, bzone::kFire}, {kPadR, bzone::kFire},
    {kPadSelect, bzone::kCoin}, {kPadStart, bzone::kStart1},
};

const ButtonName kBzoneButtons[] = {
    {"fire", bzone::kFire}, {"lfwd", bzone::kLeftFwd}, {"lback", bzone::kLeftBack},
    {"rfwd", bzone::kRightFwd}, {"rback", bzone::kRightBack},
    {"up", bzone::kLeftFwd | bzone::kRightFwd}, {"down", bzone::kLeftBack | bzone::kRightBack},
    {"left", bzone::kLeftBack | bzone::kRightFwd}, {"right", bzone::kLeftFwd | bzone::kRightBack},
    {"coin", bzone::kCoin}, {"coin2", bzone::kCoin2}, {"start1", bzone::kStart1},
    {"start2", bzone::kStart2}, {"test", bzone::kTest}, {"step", bzone::kDiagStep},
};

// ---- Zaxxon -------------------------------------------------------------------

class Zaxxon : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        using namespace zaxxon;
        m_.init({r[kRegionCpu], r[kRegionChars], r[kRegionBgGfx], r[kRegionSprites], r[kRegionMap],
                 r[kRegionProms]}, rate, swap);
    }
    // No demo sounds switch: SW1's "Sound" turns all sound off, and the
    // game is silent in its attract mode either way (zaxxon.h).
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    bool snapshot() override { return shot_.take(m_); }
    void render_snapshot_line(int y, uint16_t* out) override { shot_.render(m_, y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    zaxxon::Machine m_;
    Shot<zaxxon::Machine> shot_;
};

// The stick as the cabinet's flight stick: up = pushed forward = dive,
// down = pulled back = climb. A or B fires, Select = coin, Start = 1
// player, L = 2 players (they take turns).
const Control kZaxxonControls[] = {
    {kPadUp, zaxxon::kUp}, {kPadDown, zaxxon::kDown}, {kPadLeft, zaxxon::kLeft},
    {kPadRight, zaxxon::kRight}, {kPadA, zaxxon::kFire}, {kPadB, zaxxon::kFire},
    {kPadSelect, zaxxon::kCoin}, {kPadStart, zaxxon::kStart1}, {kPadL, zaxxon::kStart2},
};

const ButtonName kZaxxonButtons[] = {
    {"up", zaxxon::kUp}, {"down", zaxxon::kDown}, {"left", zaxxon::kLeft}, {"right", zaxxon::kRight},
    {"fire", zaxxon::kFire}, {"coin", zaxxon::kCoin}, {"coin2", zaxxon::kCoin2},
    {"start1", zaxxon::kStart1}, {"start2", zaxxon::kStart2}, {"service", zaxxon::kService},
    {"test", zaxxon::kTest},
};

// ---- Galaxian -----------------------------------------------------------------

class Galaxian : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init({r[galaxian::kRegionCpu], r[galaxian::kRegionGfx], r[galaxian::kRegionProms]}, rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    bool snapshot() override { return shot_.take(m_); }
    void render_snapshot_line(int y, uint16_t* out) override { shot_.render(m_, y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    galaxian::Machine m_;
    Shot<galaxian::Machine> shot_;
};

// Left / right and one button: A (or B) fires. Select = coin, Start = 1
// player, L = 2 players (they take turns).
const Control kGalaxianControls[] = {
    {kPadLeft, galaxian::kLeft}, {kPadRight, galaxian::kRight}, {kPadA, galaxian::kFire},
    {kPadB, galaxian::kFire}, {kPadSelect, galaxian::kCoin}, {kPadStart, galaxian::kStart1},
    {kPadL, galaxian::kStart2},
};

const ButtonName kGalaxianButtons[] = {
    {"left", galaxian::kLeft}, {"right", galaxian::kRight}, {"fire", galaxian::kFire},
    {"coin", galaxian::kCoin}, {"coin2", galaxian::kCoin2}, {"start1", galaxian::kStart1},
    {"start2", galaxian::kStart2}, {"service", galaxian::kService}, {"test", galaxian::kTest},
};

// ---- Dig Dug II (Namco's Mappy board) ----------------------------------------

class Digdug2 : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init(mappy::digdug2::kBoard, r, rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    bool snapshot() override { return shot_.take(m_); }
    void render_snapshot_line(int y, uint16_t* out) override { shot_.render(m_, y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    mappy::Machine m_;
    Shot<mappy::Machine> shot_;
};

// The 4-way stick, A = pump (button 1, the main one), B = jackhammer (button
// 2). Select = coin, Start = 1 player, L = 2 players (they take turns).
const Control kDigdug2Controls[] = {
    {kPadUp, mappy::kUp}, {kPadDown, mappy::kDown}, {kPadLeft, mappy::kLeft},
    {kPadRight, mappy::kRight}, {kPadA, mappy::kButton1}, {kPadB, mappy::kButton2},
    {kPadSelect, mappy::kCoin}, {kPadStart, mappy::kStart1}, {kPadL, mappy::kStart2},
};

const ButtonName kDigdug2Buttons[] = {
    {"up", mappy::kUp}, {"down", mappy::kDown}, {"left", mappy::kLeft}, {"right", mappy::kRight},
    {"pump", mappy::kButton1}, {"hammer", mappy::kButton2}, {"coin", mappy::kCoin},
    {"coin2", mappy::kCoin2}, {"start1", mappy::kStart1}, {"start2", mappy::kStart2},
    {"service", mappy::kService}, {"test", mappy::kTest},
};

// ---- Amidar -------------------------------------------------------------------

class Amidar : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        m_.init({r[amidar::kRegionCpu], r[amidar::kRegionSound], r[amidar::kRegionGfx], r[amidar::kRegionProms]},
                rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    // IN2's Demo Sounds switch (on by default): the attract mode's tunes.
    bool set_demo_sounds(bool on) override { m_.set_demo_sounds(on); return true; }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    bool snapshot() override { return shot_.take(m_); }
    void render_snapshot_line(int y, uint16_t* out) override { shot_.render(m_, y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    amidar::Machine m_;
    Shot<amidar::Machine> shot_;
};

// The 4-way stick and the one button, Jump (it stuns the enemies): B, and A
// too. Select = coin, Start = 1 player, L = 2 players (they take turns).
const Control kAmidarControls[] = {
    {kPadUp, amidar::kUp}, {kPadDown, amidar::kDown}, {kPadLeft, amidar::kLeft},
    {kPadRight, amidar::kRight}, {kPadB, amidar::kJump}, {kPadA, amidar::kJump},
    {kPadSelect, amidar::kCoin}, {kPadStart, amidar::kStart1}, {kPadL, amidar::kStart2},
};

const ButtonName kAmidarButtons[] = {
    {"up", amidar::kUp}, {"down", amidar::kDown}, {"left", amidar::kLeft}, {"right", amidar::kRight},
    {"jump", amidar::kJump}, {"coin", amidar::kCoin}, {"coin2", amidar::kCoin2},
    {"start1", amidar::kStart1}, {"start2", amidar::kStart2}, {"service", amidar::kService},
};

// ---- Williams: Robotron: 2084, Defender --------------------------------------

template <williams::Board B> class Williams : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override { m_.init(B, r, rate, swap); }
    void set_dips(uint8_t) override {}          // no DIP switches: settings are in the CMOS RAM
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    williams::Machine m_;
};

// Twin stick: the left stick (or d-pad) moves, the right stick fires; the
// face buttons fire too, by position (Y up, A down, X left, B right), for
// controllers without a usable right stick.
const Control kRobotronControls[] = {
    {kPadUp, williams::robotron::kUp}, {kPadDown, williams::robotron::kDown},
    {kPadLeft, williams::robotron::kLeft}, {kPadRight, williams::robotron::kRight},
    {kPadRUp, williams::robotron::kFireUp}, {kPadRDown, williams::robotron::kFireDown},
    {kPadRLeft, williams::robotron::kFireLeft}, {kPadRRight, williams::robotron::kFireRight},
    {kPadY, williams::robotron::kFireUp}, {kPadA, williams::robotron::kFireDown},
    {kPadX, williams::robotron::kFireLeft}, {kPadB, williams::robotron::kFireRight},
    {kPadSelect, williams::robotron::kCoin}, {kPadStart, williams::robotron::kStart1},
    {kPadL, williams::robotron::kStart2},
};

const ButtonName kRobotronButtons[] = {
    {"up", williams::robotron::kUp}, {"down", williams::robotron::kDown},
    {"left", williams::robotron::kLeft}, {"right", williams::robotron::kRight},
    {"fireup", williams::robotron::kFireUp}, {"firedown", williams::robotron::kFireDown},
    {"fireleft", williams::robotron::kFireLeft}, {"fireright", williams::robotron::kFireRight},
    {"coin", williams::robotron::kCoin}, {"coin2", williams::robotron::kCoin2},
    {"start1", williams::robotron::kStart1}, {"start2", williams::robotron::kStart2},
    {"advance", williams::robotron::kAdvance}, {"autoup", williams::robotron::kAutoUp},
    {"hsreset", williams::robotron::kHighScoreReset},
};

// Up / down move; left / right = the stick (reverse / thrust by the ship's
// facing, see williams.cpp); A fire, B thrust, X smart bomb, Y hyperspace,
// R reverse (the cabinet's own buttons).
const Control kDefenderControls[] = {
    {kPadUp, williams::defender::kUp}, {kPadDown, williams::defender::kDown},
    {kPadLeft, williams::defender::kLeft}, {kPadRight, williams::defender::kRight},
    {kPadA, williams::defender::kFire}, {kPadB, williams::defender::kThrust},
    {kPadX, williams::defender::kSmartBomb}, {kPadY, williams::defender::kHyperspace},
    {kPadR, williams::defender::kReverse},
    {kPadSelect, williams::defender::kCoin}, {kPadStart, williams::defender::kStart1},
    {kPadL, williams::defender::kStart2},
};

const ButtonName kDefenderButtons[] = {
    {"up", williams::defender::kUp}, {"down", williams::defender::kDown},
    {"left", williams::defender::kLeft}, {"right", williams::defender::kRight},
    {"fire", williams::defender::kFire}, {"thrust", williams::defender::kThrust},
    {"bomb", williams::defender::kSmartBomb}, {"hyper", williams::defender::kHyperspace},
    {"reverse", williams::defender::kReverse}, {"coin", williams::defender::kCoin},
    {"start1", williams::defender::kStart1}, {"start2", williams::defender::kStart2},
    {"advance", williams::defender::kAdvance}, {"autoup", williams::defender::kAutoUp},
    {"hsreset", williams::defender::kHighScoreReset},
};

// ---- Qix ---------------------------------------------------------------------

class Qix : public Machine {
public:
    bool ok() const { return m_.ok(); }
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override { m_.init(r, rate, swap); }
    void set_dips(uint8_t) override {}          // no DIP switches: settings are in the CMOS RAM
    // The operator's ATTRACT SOUND setting (CMOS 87E9; factory setting on):
    // the demo play's sounds.
    bool set_demo_sounds(bool on) override { m_.set_attract_sound(on); return true; }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return 0; }     // no watchdog on this board

private:
    qix::Machine m_;
};

// The video RAM is two 32 KB blocks of its own: null if either didn't fit.
Machine* make_qix()
{
    Qix* q = new (std::nothrow) Qix;
    if (q && !q->ok()) {
        delete q;
        q = nullptr;
    }
    return q;
}

// 4-way stick; A = Fast Draw, B = Slow Draw.
const Control kQixControls[] = {
    {kPadUp, qix::kUp}, {kPadDown, qix::kDown}, {kPadLeft, qix::kLeft}, {kPadRight, qix::kRight},
    {kPadA, qix::kFast}, {kPadB, qix::kSlow},
    {kPadSelect, qix::kCoin}, {kPadStart, qix::kStart1}, {kPadL, qix::kStart2},
};

const ButtonName kQixButtons[] = {
    {"up", qix::kUp}, {"down", qix::kDown}, {"left", qix::kLeft}, {"right", qix::kRight},
    {"fast", qix::kFast}, {"slow", qix::kSlow}, {"coin", qix::kCoin}, {"coin2", qix::kCoin2},
    {"start1", qix::kStart1}, {"start2", qix::kStart2}, {"advance", qix::kAdvance},
};

// nothrow: on the board a failed plain new aborts (no exceptions) - the
// runner says "Not enough memory" instead.
// ---- Star Wars --------------------------------------------------------------

class Starwars : public Machine {
public:
    void init(const uint8_t* const* r, uint32_t rate, bool swap) override
    {
        using namespace starwars;
        m_.init({{r[kRegionRom0], r[kRegionRom1], r[kRegionRom2], r[kRegionRom3], r[kRegionRom4]},
                 r[kRegionVector], {r[kRegionSound0], r[kRegionSound1]}, r[kRegionMath]},
                rate, swap);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    // 10D's Demo Sounds (switch 7; on by default): the attract mode's logo
    // starts a random tune or speech about every 7 minutes (starwars.h).
    bool set_demo_sounds(bool on) override { m_.set_demo_sounds(on); return true; }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    // The yoke = the left stick, in proportion (the d-pad moves the sights
    // at a steady speed, starwars::Machine::kSightSpeed).
    void set_analog(const Analog& a) override { m_.set_yoke(a.lx, a.ly); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }
    bool snapshot() override { return m_.snapshot(); }
    void render_snapshot_line(int y, uint16_t* out) override { m_.render_snapshot_line(y, out); }

private:
    starwars::Machine m_;
};

// The d-pad moves the sights at a steady speed (the left stick: the yoke
// itself); A and B fire (the yoke's triggers), X and Y too (its thumb
// buttons). The game has no start button: a trigger starts it, so Start
// fires too.
const Control kStarwarsControls[] = {
    {kPadUp, starwars::kUp}, {kPadDown, starwars::kDown}, {kPadLeft, starwars::kLeft},
    {kPadRight, starwars::kRight}, {kPadA, starwars::kFire1}, {kPadB, starwars::kFire2},
    {kPadX, starwars::kFire3}, {kPadY, starwars::kFire4}, {kPadSelect, starwars::kCoin},
    {kPadStart, starwars::kFire1},
};

const ButtonName kStarwarsButtons[] = {
    {"fire", starwars::kFire1}, {"fire2", starwars::kFire2}, {"fire3", starwars::kFire3},
    {"fire4", starwars::kFire4}, {"up", starwars::kUp}, {"down", starwars::kDown},
    {"left", starwars::kLeft}, {"right", starwars::kRight}, {"coin", starwars::kCoin},
    {"coin2", starwars::kCoin2}, {"aux", starwars::kAux}, {"start1", starwars::kFire1},
    {"test", starwars::kTest}, {"step", starwars::kDiagStep},
};

template <class M> Machine* make() { return new (std::nothrow) M; }

#define COUNT(a) int(sizeof(a) / sizeof((a)[0]))

} // namespace

const Info kGames[] = {
    {"pacman", "Pac-Man", &pacman::kRomSet, pacman::kScreenW, pacman::kScreenH, Orientation::Portrait,
     pacman::kCpuClock, pacman::kCyclesPerFrame, kPacmanControls, COUNT(kPacmanControls),
     kPacmanButtons, COUNT(kPacmanButtons), nullptr, 0, make<Pacman>, true, false, "Namco", 1980, 300},
    {"invaders", "Space Invaders", &invaders::kRomSet, invaders::kScreenW, invaders::kScreenH,
     Orientation::Portrait, invaders::kCpuClock, invaders::kCyclesPerFrame, kInvadersControls,
     COUNT(kInvadersControls), kInvadersButtons, COUNT(kInvadersButtons), invaders::kSampleNames,
     invaders::kSampleCount, make<Invaders>, false, false, "Taito", 1978, 30},
    {"rallyx", "Rally-X", &rallyx::kRomSet, rallyx::kScreenW, rallyx::kScreenH, Orientation::Landscape,
     rallyx::kCpuClock, rallyx::kCyclesPerFrame, kRallyxControls, COUNT(kRallyxControls),
     kRallyxButtons, COUNT(kRallyxButtons), nullptr, 0, make<Rallyx>, true, false, "Namco", 1980, 770},
    {"dkong", "Donkey Kong", &dkong::kRomSet, dkong::kScreenW, dkong::kScreenH, Orientation::Portrait,
     dkong::kCpuClock, dkong::kCyclesPerFrame, kDkongControls, COUNT(kDkongControls), kDkongButtons,
     COUNT(kDkongButtons), nullptr, 0, make<Dkong>, true, false, "Nintendo", 1981, 30},
    {"galaga", "Galaga", &galaga::kRomSet, galaga::kScreenW, galaga::kScreenH, Orientation::Portrait,
     galaga::kCpuClock, galaga::kCyclesPerFrame, kNamcoControls, COUNT(kNamcoControls), kNamcoButtons,
     COUNT(kNamcoButtons), nullptr, 0, make<Galaga>, false, false, "Namco", 1981, 870},
    {"digdug", "Dig Dug", &digdug::kRomSet, digdug::kScreenW, digdug::kScreenH, Orientation::Portrait,
     digdug::kCpuClock, digdug::kCyclesPerFrame, kNamcoControls, COUNT(kNamcoControls), kNamcoButtons,
     COUNT(kNamcoButtons), nullptr, 0, make<Digdug>, true, false, "Namco", 1982, 700},
    {"mspacman", "Ms. Pac-Man", &mspacman::kRomSet, pacman::kScreenW, pacman::kScreenH, Orientation::Portrait,
     pacman::kCpuClock, pacman::kCyclesPerFrame, kPacmanControls, COUNT(kPacmanControls),
     kPacmanButtons, COUNT(kPacmanButtons), nullptr, 0, make<Mspacman>, true, false, "Midway", 1982, 300},
    {"mrdo", "Mr. Do!", &mrdo::kRomSet, mrdo::kScreenW, mrdo::kScreenH, Orientation::Portrait,
     mrdo::kCpuClock, mrdo::kCyclesPerFrame, kMrdoControls, COUNT(kMrdoControls), kMrdoButtons,
     COUNT(kMrdoButtons), nullptr, 0, make<Mrdo>, true, false, "Universal", 1982, 30},
    {"btime", "Burger Time", &btime::kRomSet, btime::kScreenW, btime::kScreenH, Orientation::Portrait,
     btime::kCpuClock, btime::kCyclesPerFrame, kBtimeControls, COUNT(kBtimeControls), kBtimeButtons,
     COUNT(kBtimeButtons), nullptr, 0, make<Btime>, true, false, "Data East", 1982, 30},
    {"qbert", "Q*bert", &qbert::kRomSet, qbert::kScreenW, qbert::kScreenH, Orientation::Portrait,
     qbert::kCpuClock, qbert::kCyclesPerFrame, kQbertControls, COUNT(kQbertControls), kQbertButtons,
     COUNT(kQbertButtons), nullptr, 0, make<Qbert>, false, true, "Gottlieb", 1982, 30},
    {"asteroid", "Asteroids", &asteroids::kRomSet, asteroids::kScreenW, asteroids::kScreenH,
     Orientation::Landscape, asteroids::kCpuClock, asteroids::kCyclesPerFrame, kAsteroidsControls,
     COUNT(kAsteroidsControls), kAsteroidsButtons, COUNT(kAsteroidsButtons), nullptr, 0, make<Asteroids>,
     false, false, "Atari", 1979, 30},
    {"tempest", "Tempest", &tempest::kRomSet, tempest::kScreenW, tempest::kScreenH, Orientation::Landscape,
     tempest::kCpuClock, tempest::kCyclesPerFrame, kTempestControls, COUNT(kTempestControls),
     kTempestButtons, COUNT(kTempestButtons), nullptr, 0, make<Tempest>, false, false, "Atari", 1981, 30},
    {"robotron", "Robotron: 2084", &williams::robotron::kRomSet, williams::kScreenW, williams::kScreenH,
     Orientation::Landscape, williams::kCpuClock, williams::kCyclesPerFrame, kRobotronControls,
     COUNT(kRobotronControls), kRobotronButtons, COUNT(kRobotronButtons), nullptr, 0,
     make<Williams<williams::Board::Robotron>>, false, false, "Williams", 1982, 760},
    {"defender", "Defender", &williams::defender::kRomSet, williams::kScreenW, williams::kScreenH,
     Orientation::Landscape, williams::kCpuClock, williams::kCyclesPerFrame, kDefenderControls,
     COUNT(kDefenderControls), kDefenderButtons, COUNT(kDefenderButtons), nullptr, 0,
     make<Williams<williams::Board::Defender>>, false, false, "Williams", 1981, 1625},
    {"qix", "Qix", &qix::kRomSet, qix::kScreenW, qix::kScreenH, Orientation::Portrait, qix::kCpuClock,
     qix::kCyclesPerFrame, kQixControls, COUNT(kQixControls), kQixButtons, COUNT(kQixButtons), nullptr, 0,
     make_qix, true, false, "Taito", 1981, 190},
    {"starwars", "Star Wars", &starwars::kRomSet, starwars::kScreenW, starwars::kScreenH,
     Orientation::Landscape, starwars::kCpuClock, starwars::kCyclesPerFrame, kStarwarsControls,
     COUNT(kStarwarsControls), kStarwarsButtons, COUNT(kStarwarsButtons), nullptr, 0, make<Starwars>, false, false, "Atari", 1983, 90},
    {"missile", "Missile Command", &missile::kRomSet, missile::kScreenW, missile::kScreenH,
     Orientation::Landscape, missile::kClock, missile::kTicksPerFrame, kMissileControls,
     COUNT(kMissileControls), kMissileButtons, COUNT(kMissileButtons), nullptr, 0, make<Missile>, false, false,
     "Atari", 1980, 48},
    {"bzone", "Battlezone", &bzone::kRomSet, bzone::kScreenW, bzone::kScreenH, Orientation::Landscape,
     bzone::kCpuClock, bzone::kCyclesPerFrame, kBzoneControls, COUNT(kBzoneControls), kBzoneButtons,
     COUNT(kBzoneButtons), nullptr, 0, make<Bzone>, false, false, "Atari", 1980, 70},
    {"zaxxon", "Zaxxon", &zaxxon::kRomSet, zaxxon::kScreenW, zaxxon::kScreenH, Orientation::Portrait,
     zaxxon::kCpuClock, zaxxon::kCyclesPerFrame, kZaxxonControls, COUNT(kZaxxonControls), kZaxxonButtons,
     COUNT(kZaxxonButtons), nullptr, 0, make<Zaxxon>, false, false, "Sega", 1982, 30},
    {"galaxian", "Galaxian", &galaxian::kRomSet, galaxian::kScreenW, galaxian::kScreenH, Orientation::Portrait,
     galaxian::kCpuClock, galaxian::kCyclesPerFrame, kGalaxianControls, COUNT(kGalaxianControls),
     kGalaxianButtons, COUNT(kGalaxianButtons), nullptr, 0, make<Galaxian>, false, false, "Namco", 1979, 216},
    {"amidar", "Amidar", &amidar::kRomSet, amidar::kScreenW, amidar::kScreenH, Orientation::Portrait,
     amidar::kCpuClock, amidar::kCyclesPerFrame, kAmidarControls, COUNT(kAmidarControls), kAmidarButtons,
     COUNT(kAmidarButtons), nullptr, 0, make<Amidar>, true, false, "Konami", 1981, 193},
    {"digdug2", "Dig Dug II", &mappy::digdug2::kRomSet, mappy::kScreenW, mappy::kScreenH, Orientation::Portrait,
     mappy::kCpuClock, mappy::kCyclesPerFrame, kDigdug2Controls, COUNT(kDigdug2Controls), kDigdug2Buttons,
     COUNT(kDigdug2Buttons), nullptr, 0, make<Digdug2>, true, false, "Namco", 1985, 286},

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

uint32_t diagonal_pad(uint32_t pad)
{
    constexpr uint32_t kDirs = kPadUp | kPadDown | kPadLeft | kPadRight;
    const uint32_t d = pad & kDirs;
    uint32_t keep;
    if ((d & (kPadUp | kPadRight)) == (kPadUp | kPadRight))           keep = kPadUp;
    else if ((d & (kPadRight | kPadDown)) == (kPadRight | kPadDown))  keep = kPadRight;
    else if ((d & (kPadDown | kPadLeft)) == (kPadDown | kPadLeft))    keep = kPadDown;
    else if ((d & (kPadLeft | kPadUp)) == (kPadLeft | kPadUp))        keep = kPadLeft;
    else if (d == kPadUp || d == kPadRight || d == kPadDown || d == kPadLeft) keep = d;
    else keep = 0;                                  // none, or opposite ones only
    return (pad & ~kDirs) | keep;
}

uint16_t map_pad(const Info& game, uint32_t pad)
{
    if (game.diagonal) pad = diagonal_pad(pad);
    uint16_t b = 0;
    for (int i = 0; i < game.control_count; ++i)
        if (pad & game.controls[i].pad) b |= game.controls[i].machine;
    return b;
}

} // namespace games
