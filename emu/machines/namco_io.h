// Namco custom I/O chips of the Galaga / Dig Dug CPU board, written for this
// project (MIT): the 06XX bus interface and behaviour models of the 51XX
// (controls, coins, credits) and 53XX (DIP switch reader). The 51XX and
// 53XX are Fujitsu MB884x microcontrollers whose programs (51xx.bin,
// 53xx.bin) aren't in the usual ROM sets, so what they do is modelled from
// their known behaviour instead of running them. Hardware facts from public
// documentation and MAME's drivers (read for facts only).
//
// The 06XX sits between the main CPU and up to four of these chips. The
// CPU writes its control register (low 4 bits = chip selects, bit 4 = read,
// top 3 bits = clock divider); the 06XX then pulses the main CPU's NMI
// once per divided clock period, and the NMI handler moves one byte through
// the data register per pulse, until the CPU writes a control value with a
// zero divider (0x10) to stop it.
#pragma once

#include <cstdint>

namespace namco_io {

// --- 06XX ------------------------------------------------------------------

class Bus06 {
public:
    static constexpr uint64_t kNever = ~uint64_t(0);
    // The 06XX is clocked at the CPU clock / 64 (48 kHz on a 3.072 MHz board).
    static constexpr int kCpuCyclesPerClock = 64;

    void reset()
    {
        control_ = 0;
        next_nmi_ = kNever;
    }

    // A control register write at CPU cycle `now` (counted from power-on). A
    // divider of 0 stops the NMIs; otherwise the first NMI comes on the next
    // 06XX clock, one period later for a read (the chip gets a cycle to put
    // its first byte out).
    void set_control(uint8_t v, uint64_t now);
    uint8_t control() const { return control_; }

    uint8_t selected() const { return control_ & 0x0f; }   // chip select bits
    bool    reading() const { return (control_ & 0x10) != 0; }
    // NMI period in CPU cycles: 64 << divider.
    int     period() const { return kCpuCyclesPerClock << (control_ >> 5); }

    // CPU cycle of the next NMI pulse (kNever = none); after delivering it,
    // call nmi_taken().
    uint64_t next_nmi() const { return next_nmi_; }
    void     nmi_taken() { next_nmi_ += uint64_t(period()); }

private:
    uint8_t  control_ = 0;
    uint64_t next_nmi_ = kNever;
};

// --- 51XX --------------------------------------------------------------------

// Inputs as the 51XX sees them (active low, like the cabinet's switches):
//   sticks: bits 0-3 player 1 up / right / down / left, bits 4-7 player 2
//   buttons: bit 0 fire 1, 1 fire 2, 2 start 1, 3 start 2, 4 coin 1,
//            5 coin 2, 6 service credit, 7 test switch
//
// Commands (written bytes, low 3 bits): 1 + 4 bytes = coinage (coins per
// credit and credits per coin for slots 1 and 2), 2 = credit mode with the
// start buttons live, 3 / 4 = joystick remapping off / on, 5 = switch mode.
// Reads go round in threes:
//   switch mode: buttons, sticks, 0
//   credit mode: credits in BCD (0xBB while the test switch is on), then
//                player 1 and player 2: stick (raw, or remapped to a
//                direction 0-8) in bits 0-3, fire "just pressed" in bit 4
//                and fire held in bit 5 (both active low)
// Coins and the start buttons count on the read of the credits byte: a coin
// adds credits by the coinage, start 1 / 2 takes 1 / 2 credits and leaves
// credit mode (the start buttons go dead until the next command 2). With 0
// coins per credit it is free play (100 credits shown as 0xA0).
class Chip51 {
public:
    void reset();
    void set_inputs(uint8_t sticks, uint8_t buttons)
    {
        sticks_ = sticks;
        buttons_ = buttons;
    }
    void    write(uint8_t data);
    uint8_t read();
    // Xevious: command 1 is followed by 6 bytes (the last 4 the coinage)
    // and turns the joystick remapping on - the game never sends command 4
    // but reads directions (as old MAME's 51XX model did for it).
    void    set_xevious(bool on) { xevious_ = on; }

    int credits() const { return credits_; }
    int mode() const { return mode_; }      // 0 switch, 1 credit, 2 credit (playing)

private:
    uint8_t player(int p);

    uint8_t sticks_ = 0xff, buttons_ = 0xff;
    uint8_t mode_ = 0;
    uint8_t coinage_left_ = 0;              // coinage bytes still to come
    uint8_t coins_per_credit_[2] = {1, 1};
    uint8_t credits_per_coin_[2] = {1, 1};
    uint8_t coins_[2] = {};
    uint8_t last_buttons_ = 0;              // pressed = 1, as of the last credits read
    uint8_t last_fire_ = 0;                 // pressed = 1, per player bit
    uint8_t count_ = 0;                     // read position in the 3-byte cycle
    bool    remap_ = false;
    bool    xevious_ = false;
    int     credits_ = 0;
};

// The 51XX's joystick remapping: active-low stick bits (up, right, down,
// left) -> direction 0 = up, clockwise to 7 = up-left, 8 = centred.
extern const uint8_t kJoyMap[16];

// --- 53XX --------------------------------------------------------------------

// Dig Dug's DIP switch reader: reads alternate between bank A and bank B
// (both as wired: bit n = switch n).
class Chip53 {
public:
    void reset() { count_ = 0; }
    void set_dips(uint8_t a, uint8_t b) { dips_[0] = a; dips_[1] = b; }
    uint8_t read() { return dips_[count_++ & 1]; }

private:
    uint8_t dips_[2] = {};
    uint8_t count_ = 0;
};

// --- 50XX --------------------------------------------------------------------

// The score keeper (an MB8842 with its own program, 50xx.bin, not in the
// usual sets): Bosconian keeps its scores in it, Xevious only checks it
// at power-on (protection). Modelled from the documented command set:
//   0x nothing, 1x scores to 0, 2x / 3x / 5x + 3 BCD bytes (most
//   significant first) = first bonus / bonus interval / high score,
//   60 / 68 = player 1 / 2, 70 = adding, 71-7F = taking away,
//   8x-Fx = add (or take away) an amount: 8x 5 10 15 20 25 30 40 50 60 70
//   80 90 100 200 300 500, Bx 10 20 30 40 50 60 80 100 120 ... 200 400 600
//   1000, Ex 15 30 45 ... 300 600 900 1500; 9x / Cx / Fx x10, Ax / Dx x100.
// Reads give the selected player's score, 7 BCD digits in 4 bytes: the
// first holds the flags (80 high score, 40 first bonus, 20 a further
// bonus - set when the score passes them (the high score only once one
// was given), cleared once read) and the top
// digit; the reads start over at each 06XX control write that selects it.
// Assumed (the program isn't known): the flags' exact timing, a score
// taken below 0 stays 0.
class Chip50 {
public:
    void reset();
    void select() { count_ = 0; }           // a 06XX control write with this chip selected
    void write(uint8_t data);
    uint8_t read();

    uint32_t score(int player) const { return score_[player & 1]; }   // binary

private:
    void add(uint32_t amount);

    uint32_t score_[2] = {};
    uint32_t first_ = 0, interval_ = 0, high_ = 0, next_bonus_[2] = {};
    bool     high_set_ = false;             // a 5x command gave the high score
    uint8_t  flags_[2] = {};
    uint8_t  player_ = 0;
    bool     subtract_ = false;
    uint8_t  param_cmd_ = 0, params_left_ = 0;
    uint32_t param_ = 0;
    uint8_t  count_ = 0;
};

} // namespace namco_io
