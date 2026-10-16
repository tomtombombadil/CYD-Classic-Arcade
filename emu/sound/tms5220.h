// Texas Instruments TMS5220 speech synthesiser, written for this project
// (MIT). Star Wars' sound CPU feeds it LPC frames through its 16-byte FIFO
// ("Speak External"); the chip turns them into speech at clock / 80 samples
// a second (8.4 kHz on Star Wars).
//
// How it works (US patents 4,209,836 / 4,331,836 / 4,335,277, the
// datasheet, and MAME's notes on the decapped chips - read for facts only):
// a frame is 4 energy bits (0 = silence, 15 = stop), a repeat bit, 6 pitch
// bits (0 = unvoiced) and the reflection coefficients K1-K10 as table
// indices (5 5 4 4 4 4 4 3 3 3 bits; a repeat frame keeps the old K's, an
// unvoiced one has K1-K4 only). Each frame lasts 200 samples: 8
// interpolation periods (IP) of 25 samples, in which the parameters step
// towards the new frame's values by 1/8 1/8 1/8 1/4 1/4 1/2 1/2 1 of the
// difference, one parameter at a time. Voiced sound is excited by the
// chirp ROM replayed every pitch period, unvoiced by a 13-bit LFSR, scaled
// by the energy and run through a 10-stage lattice filter in the chip's
// fixed-point arithmetic; the analog output is the result clipped to 8
// bits. Interpolation is skipped (the values jump at the end of the
// frame) between voiced and unvoiced frames and out of silence.
//
// The host's bus: /WS strobes a byte in (a command, or in Speak External
// mode a FIFO byte) 16 chip clocks later; /RS latches the status (talk,
// buffer low, buffer empty) 13 us later; /READY is low once either is done.
#pragma once

#include <cstdint>

namespace tms5220 {

// Chip ROM tables (as decapped; exposed for tests).
extern const uint8_t kEnergy[16];
extern const uint8_t kPitch[64];
extern const int16_t kK[10][32];
extern const int8_t  kChirp[52];
extern const uint8_t kKBits[10];

class Chip {
public:
    // `chip_clock` (Hz), `host_clock`: the clock of the time stamps given
    // to the bus functions; `out_rate`: render()'s sample rate.
    void init(uint32_t chip_clock, uint32_t host_clock, uint32_t out_rate);
    void reset();

    // --- the bus, with host time stamps (any origin, never going back) ---
    // /RS and /WS levels (true = high = inactive).
    void strobes(bool rs, bool ws, int64_t now);
    void set_data(uint8_t v) { write_latch_ = v; }  // what the host drives
    uint8_t data() const { return read_latch_; }    // what the chip drives (/RS low)
    bool ready() const { return io_ready_; }        // /READY low
    // A pending bus action (a byte written, the status latched) falls due
    // at event_at() (-1 = none); the host calls fire() at that time, after
    // rendering up to it (writes reach the FIFO in time order).
    int64_t event_at() const { return event_at_; }
    void fire();

    // A byte straight in (as /WS does it), and the status (as /RS latches
    // it) - for tests.
    void write(uint8_t v);
    uint8_t status() const;

    // n output samples (the chip's 8-bit output as +-32512, interpolated
    // to the output rate), times gain / 256. add = mix into `out`.
    void render(int16_t* out, int n, bool add = false, int gain = 256);
    // One chip sample (tests).
    int16_t step();

    bool talking() const { return spen_ || talkd_; }
    bool speak_external() const { return ddis_; }
    int  fifo_count() const { return fifo_count_; }
    uint32_t frames() const { return frames_; }    // frames parsed so far

private:
    void update_status();
    int  read_bits(int n);
    void parse_frame();
    void start_frame_state();
    int32_t lattice(int32_t excitation);

    // Bus
    uint8_t  write_latch_ = 0, read_latch_ = 0xff;
    bool     rs_ = true, ws_ = true;
    bool     io_ready_ = true;
    int64_t  event_at_ = -1;
    bool     event_write_ = false;
    int64_t  clock16_ = 36;         // 16 chip clocks in host clocks
    int64_t  read_delay_ = 20;      // 13 us in host clocks

    // FIFO
    uint8_t  fifo_[16] = {};
    uint8_t  fifo_head_ = 0, fifo_count_ = 0, fifo_bits_ = 0;

    // Status
    bool spen_ = false, ddis_ = false, talk_ = false, talkd_ = false;
    bool buffer_low_ = true, buffer_empty_ = true, prev_talk_ = false;

    // Frame parameters: the new frame's indices, the current values.
    uint8_t  new_e_ = 0, new_p_ = 0, new_k_[10] = {};
    bool     zpar_ = false, uv_zpar_ = false;
    bool     old_e0_ = true, old_p0_ = true;    // the old frame was silent / unvoiced
    bool     inhibit_ = true, pitch_zero_ = false;
    int32_t  energy_ = 0, pitch_ = 0, k_[10] = {};
    int32_t  prev_energy_ = 0;
    // Timing: sub-cycle (1-2), parameter counter PC (0-12), IP (0-7).
    uint8_t  sub_ = 1, pc_ = 0, ip_ = 0;
    int32_t  pitch_count_ = 0;
    uint16_t rng_ = 0x1fff;
    int32_t  u_[11] = {}, x_[10] = {};
    uint32_t frames_ = 0;

    // Output: chip samples interpolated to the output rate.
    uint32_t step_ = 0, frac_ = 0;  // chip samples per output sample, 16.16
    int16_t  last_ = 0, prev_ = 0;
};

} // namespace tms5220
