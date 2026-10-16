// TMS5220 speech synthesiser - see tms5220.h.
#include "tms5220.h"

namespace tms5220 {

// The chip's ROM tables, as read from the decapped TMS5220 (published
// numbers; the same as the TMS5110A's LPC table).
const uint8_t kEnergy[16] = {0, 1, 2, 3, 4, 6, 8, 11, 16, 23, 33, 47, 63, 85, 114, 0};

const uint8_t kPitch[64] = {
    0,   15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
    30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  44,  46,  48,
    50,  52,  53,  56,  58,  60,  62,  65,  68,  70,  72,  76,  78,  80,  84,  86,
    91,  94,  98,  101, 105, 109, 114, 118, 122, 127, 132, 137, 142, 148, 153, 159,
};

const int16_t kK[10][32] = {
    {-501, -498, -497, -495, -493, -491, -488, -482, -478, -474, -469, -464, -459, -452, -445, -437,
     -412, -380, -339, -288, -227, -158, -81,  -1,   80,   157,  226,  287,  337,  379,  411,  436},
    {-328, -303, -274, -244, -211, -175, -138, -99,  -59,  -18,  24,   64,   105,  143,  180,  215,
     248,  278,  306,  331,  354,  374,  392,  408,  422,  435,  445,  455,  463,  470,  476,  506},
    {-441, -387, -333, -279, -225, -171, -117, -63, -9, 45, 98, 152, 206, 260, 314, 368},
    {-328, -273, -217, -161, -106, -50, 5, 61, 116, 172, 228, 283, 339, 394, 450, 506},
    {-328, -282, -235, -189, -142, -96, -50, -3, 43, 90, 136, 182, 229, 275, 322, 368},
    {-256, -212, -168, -123, -79, -35, 10, 54, 98, 143, 187, 232, 276, 320, 365, 409},
    {-308, -260, -212, -164, -117, -69, -21, 27, 75, 122, 170, 218, 266, 314, 361, 409},
    {-256, -161, -66, 29, 124, 219, 314, 409},
    {-256, -176, -96, -15, 65, 146, 226, 307},
    {-205, -132, -59, 14, 87, 160, 234, 307},
};

// The chirp: one glottal pulse, then zeros (the address counter stops at
// entry 51).
const int8_t kChirp[52] = {
    0x00, 0x03, 0x0f, 0x28, 0x4c, 0x6c, 0x71, 0x50, 0x25, 0x26, 0x4c, 0x44, 0x1a,
    0x32, 0x3b, 0x13, 0x37, 0x1a, 0x25, 0x1f, 0x1d,
};

const uint8_t kKBits[10] = {5, 5, 4, 4, 4, 4, 4, 3, 3, 3};

namespace {

// Right shift of the difference per interpolation period.
constexpr uint8_t kInterpShift[8] = {0, 3, 3, 3, 2, 2, 1, 1};

// The multiplier: a 10-bit coefficient times a 15-bit value, >> 9.
inline int32_t mul(int32_t k, int32_t v)
{
    k = ((k + 512) & 1023) - 512;
    v = ((v + 16384) & 32767) - 16384;
    return (k * v) >> 9;
}

} // namespace

void Chip::init(uint32_t chip_clock, uint32_t host_clock, uint32_t out_rate)
{
    clock16_ = int64_t(host_clock) * 16 / chip_clock;
    read_delay_ = int64_t(host_clock) * 13 / 1000000 + 1;
    // Chip samples (clock / 80) per output sample, 16.16.
    step_ = uint32_t((uint64_t(chip_clock) << 16) / (80ull * (out_rate ? out_rate : 24000)));
    rs_ = ws_ = true;
    io_ready_ = true;
    event_at_ = -1;
    read_latch_ = 0xff;
    reset();
}

void Chip::reset()
{
    for (uint8_t& b : fifo_) b = 0;
    fifo_head_ = fifo_count_ = fifo_bits_ = 0;
    spen_ = ddis_ = talk_ = talkd_ = prev_talk_ = false;
    buffer_low_ = buffer_empty_ = true;
    new_e_ = new_p_ = 0;
    for (uint8_t& k : new_k_) k = 0;
    zpar_ = uv_zpar_ = false;
    old_e0_ = old_p0_ = true;
    inhibit_ = true;
    pitch_zero_ = false;
    energy_ = pitch_ = prev_energy_ = 0;
    for (int32_t& k : k_) k = 0;
    sub_ = 1;
    pc_ = ip_ = 0;
    pitch_count_ = 0;
    rng_ = 0x1fff;
    for (int32_t& v : u_) v = 0;
    for (int32_t& v : x_) v = 0;
    frac_ = 0;
    last_ = prev_ = 0;
}

// --- the bus -----------------------------------------------------------------

void Chip::strobes(bool rs, bool ws, int64_t now)
{
    const bool rs_fell = rs_ && !rs, ws_fell = ws_ && !ws;
    rs_ = rs;
    ws_ = ws;
    if (rs && ws) {                 // neither: the data pins float
        read_latch_ = 0xff;
        return;
    }
    if (!rs && !ws) return;         // both: not allowed on the TMS5220
    if (ws_fell) {
        // /READY goes high at once; the byte is taken 16 chip clocks later.
        io_ready_ = false;
        event_write_ = true;
        event_at_ = now + clock16_;
    } else if (rs_fell) {
        io_ready_ = false;
        event_write_ = false;
        event_at_ = now + read_delay_;
    }
}

void Chip::fire()
{
    if (event_at_ < 0) return;
    if (event_write_) {
        if (ddis_ && fifo_count_ >= 16) {   // FIFO full: try again later
            event_at_ += clock16_;
            return;
        }
        write(write_latch_);
    } else {
        read_latch_ = status();
    }
    io_ready_ = true;
    event_at_ = -1;
}

uint8_t Chip::status() const
{
    return uint8_t((talking() ? 0x80 : 0) | (buffer_low_ ? 0x40 : 0) | (buffer_empty_ ? 0x20 : 0));
}

// The buffer flags; an empty FIFO in Speak External ends the speech at once.
void Chip::update_status()
{
    buffer_low_ = fifo_count_ <= 8;
    buffer_empty_ = fifo_count_ == 0;
    if (buffer_empty_ && ddis_) talk_ = spen_ = false;
    if (prev_talk_ && !talking()) ddis_ = false;
    prev_talk_ = talking();
}

// The parameters a new utterance starts from: silence (zeroed), K5-K7 at
// index 15 and K8-K10 at 7 - the chip's idle state.
void Chip::start_frame_state()
{
    zpar_ = uv_zpar_ = true;
    old_e0_ = old_p0_ = true;
    new_e_ = new_p_ = 0;
    for (int i = 0; i < 4; ++i) new_k_[i] = 0;
    for (int i = 4; i < 7; ++i) new_k_[i] = 15;
    for (int i = 7; i < 10; ++i) new_k_[i] = 7;
}

void Chip::write(uint8_t v)
{
    if (ddis_) {
        if (fifo_count_ < 16) {
            const bool was_low = buffer_low_;
            fifo_[(fifo_head_ + fifo_count_) & 15] = v;
            ++fifo_count_;
            update_status();
            // Past half full for the first time: speech is enabled.
            if (!spen_ && was_low && !buffer_low_) {
                start_frame_state();
                spen_ = true;
            }
        }
        return;
    }
    switch (v & 0x70) {
    case 0x50:                      // Speak (from the speech ROMs - none here)
        start_frame_state();
        spen_ = true;
        ddis_ = false;
        break;
    case 0x60:                      // Speak External: clear the FIFO, wait for 9 bytes
        for (uint8_t& b : fifo_) b = 0;
        fifo_head_ = fifo_count_ = fifo_bits_ = 0;
        ddis_ = true;
        start_frame_state();
        break;
    case 0x70:                      // Reset
        reset();
        break;
    default:                        // Read Byte, Read and Branch, Load Address (no ROMs), NOP
        break;
    }
    update_status();
}

// Bits from the FIFO, each byte's lowest bit first, the field built MSB
// first; past the end they read 0. Without Speak External (no speech ROMs
// on the board) the bus floats high: all ones, a stop frame.
int Chip::read_bits(int n)
{
    if (!ddis_) return (1 << n) - 1;
    int v = 0;
    while (n--) {
        v = (v << 1) | ((fifo_[fifo_head_] >> fifo_bits_) & 1);
        if (++fifo_bits_ == 8) {
            fifo_bits_ = 0;
            if (fifo_count_) {
                fifo_[fifo_head_] = 0;
                fifo_head_ = (fifo_head_ + 1) & 15;
                --fifo_count_;
            }
            update_status();
        }
    }
    return v;
}

void Chip::parse_frame()
{
    ++frames_;
    zpar_ = uv_zpar_ = false;
    update_status();
    if (ddis_ && buffer_empty_) return;
    new_e_ = uint8_t(read_bits(4));
    if (ddis_ && buffer_empty_) return;
    if (new_e_ == 0 || new_e_ == 15) return;
    const bool repeat = read_bits(1) != 0;
    new_p_ = uint8_t(read_bits(6));
    uv_zpar_ = new_p_ == 0;         // unvoiced: K5-K10 are zero
    if (ddis_ && buffer_empty_) return;
    if (repeat) return;
    for (int i = 0; i < 4; ++i) {
        new_k_[i] = uint8_t(read_bits(kKBits[i]));
        if (ddis_ && buffer_empty_) return;
    }
    if (new_p_ == 0) return;
    for (int i = 4; i < 10; ++i) {
        new_k_[i] = uint8_t(read_bits(kKBits[i]));
        if (ddis_ && buffer_empty_) return;
    }
}

// The 10-stage lattice in the chip's arithmetic (10-bit K's, 14-bit sums).
int32_t Chip::lattice(int32_t excitation)
{
    u_[10] = mul(prev_energy_, excitation * 64);
    for (int i = 9; i >= 0; --i) u_[i] = u_[i + 1] - mul(k_[i], x_[i]);
    for (int i = 9; i >= 1; --i) x_[i] = x_[i - 1] + mul(k_[i - 1], u_[i - 1]);
    x_[0] = u_[0];
    prev_energy_ = energy_;
    return u_[0];
}

int16_t Chip::step()
{
    if (!talkd_) {
        // Idle: the counters run on; TALK latches at the end of a frame.
        if (++sub_ == 2 && pc_ == 12) {
            if (ip_ == 7) {
                talkd_ = talk_;
                update_status();
                if (!talk_ && spen_) talk_ = true;
            }
            sub_ = 1;
            pc_ = 0;
            ip_ = (ip_ + 1) & 7;
        } else if (sub_ == 3) {
            sub_ = 1;
            ++pc_;
        }
        return 0;
    }

    if (ip_ == 0 && pc_ == 12 && sub_ == 1) {
        // A new frame becomes the target.
        parse_frame();
        if (new_e_ == 15) {         // stop: TALK and SPEN off; TALKD while the energy ramps down
            talk_ = spen_ = false;
            update_status();
        }
        const bool new_unvoiced = new_p_ == 0, new_silent = new_e_ == 0;
        inhibit_ = (old_p0_ != new_unvoiced) || (old_e0_ && !new_silent) || (old_p0_ && new_silent);
    } else if (sub_ == 2) {
        // One parameter a step moves towards the target (all the way at IP 0).
        const bool inh = inhibit_ && ip_ != 0;
        const int sh = kInterpShift[ip_];
        if (pc_ == 0) {
            if (ip_ == 0) pitch_zero_ = false;
            const int32_t t = kEnergy[new_e_];
            energy_ = zpar_ ? 0 : energy_ + (inh ? 0 : (t - energy_) >> sh);
        } else if (pc_ == 1) {
            const int32_t t = kPitch[new_p_];
            pitch_ = zpar_ ? 0 : pitch_ + (inh ? 0 : (t - pitch_) >> sh);
        } else if (pc_ <= 11) {
            const int i = pc_ - 2;
            const bool z = i < 4 ? zpar_ : uv_zpar_;
            const int32_t t = kK[i][new_k_[i]];
            k_[i] = z ? 0 : k_[i] + (inh ? 0 : (t - k_[i]) >> sh);
        }
    }

    // Excitation: noise for unvoiced frames, the chirp for voiced ones.
    int32_t ex;
    if (old_p0_) ex = (rng_ & 1) ? -64 : 64;
    else         ex = kChirp[pitch_count_ < 51 ? pitch_count_ : 51];
    for (int i = 0; i < 20; ++i) {  // the LFSR steps once a chip T-cycle
        const int bit = ((rng_ >> 12) ^ (rng_ >> 3) ^ (rng_ >> 2) ^ rng_) & 1;
        rng_ = uint16_t(((rng_ << 1) | bit) & 0x1fff);
    }
    int32_t s = lattice(ex);
    s = ((s + 16384) & 32767) - 16384;                  // 14 bits (the K1 stage may overflow)
    if (s > 2047) s = 2047;                             // the analog output: 8 bits, clipped
    if (s < -2048) s = -2048;
    const int16_t out = int16_t((s & ~15) * 16);

    if (++sub_ == 2 && pc_ == 12) {
        if (ip_ == 7) {
            if (inhibit_) pitch_zero_ = true;
            old_e0_ = new_e_ == 0;
            old_p0_ = new_p_ == 0;
            talkd_ = talk_;
            update_status();
            if (!talk_ && spen_) talk_ = true;
        }
        sub_ = 1;
        pc_ = 0;
        ip_ = (ip_ + 1) & 7;
    } else if (sub_ == 3) {
        sub_ = 1;
        ++pc_;
    }
    if (++pitch_count_ >= pitch_ || pitch_zero_) pitch_count_ = 0;
    pitch_count_ &= 0x1ff;
    return out;
}

void Chip::render(int16_t* out, int n, bool add, int gain)
{
    for (int i = 0; i < n; ++i) {
        frac_ += step_;
        while (frac_ >= 0x10000) {
            frac_ -= 0x10000;
            prev_ = last_;
            last_ = step();
        }
        // Between the last two chip samples.
        int v = prev_ + int((int64_t(last_ - prev_) * int32_t(frac_)) >> 16);
        v = (v * gain) >> 8;
        if (add) {
            const int s = out[i] + v;
            out[i] = int16_t(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        } else {
            out[i] = int16_t(v);
        }
    }
}

} // namespace tms5220
