// The Qix sound board - see qix_sound.h.
#include "qix_sound.h"

#include <cmath>

namespace qix_sound {

namespace {

// DAC level (0-255, its middle at 128) to output at full volume: the
// loudest sounds (the DAC's whole swing) peak at ~+-21,000 after the
// high-pass.
constexpr float kScale = 120.0f;

// One attenuator: each bit puts a resistor (+250 ohm of switch) across
// the bottom of a divider with 10k on top; none = no attenuation.
float attenuation(unsigned bits)
{
    if (!bits) return 1.0f;
    const double ohms[4] = {22250.0, 10250.0, 5850.0, 3550.0};
    double g = 0;
    for (int i = 0; i < 4; ++i)
        if (bits & (1u << i)) g += 1.0 / ohms[i];
    const double r = 1.0 / g;
    return float(r / (r + 10000.0));
}

} // namespace

void Board::init(const uint8_t* rom, uint32_t audio_rate)
{
    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.map_ram(0x0000, 0x400, ram_);
    cpu_.map_read(0xf800, 0x800, rom);
    pia_.ctx = this;
    pia_.out_a = port_a_out;
    pia_.out_b = port_b_out;
    pia_.irq_changed = irq_changed;
    // The output coupling: 1.5k into 1 uF.
    hp_a_ = float(std::exp(-1.0 / (1500.0 * 1e-6 * double(audio_rate))));
    reset();
}

void Board::reset()
{
    pia_.reset();
    pia_.in_a = 0xff;
    pia_.in_b = 0x00;
    ca2_ = true;
    cpu_.irq(false);
    cpu_.reset();
}

// 0000-03FF RAM (the 6802's own is 0000-007F), 4000-7FFF the PIA, F800-FFFF
// U27; the rest reads nothing.
uint8_t Board::mem_read(void* ctx, uint16_t addr)
{
    Board& b = *static_cast<Board*>(ctx);
    if ((addr & 0xc000) == 0x4000) return b.pia_.read(addr & 3);
    return 0;
}

void Board::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Board& b = *static_cast<Board*>(ctx);
    if ((addr & 0xc000) != 0x4000) return;
    b.pia_.write(addr & 3, v);
    if ((addr & 3) == 1) {                  // CRA: CA2 may have changed
        const bool now = b.ca2();
        if (now != b.ca2_) {
            b.ca2_ = now;
            if (b.ca2_changed) b.ca2_changed(b.owner, now);
        }
    }
}

// Port A pins as the other side sees them: output bits driven, the rest
// pulled up.
uint8_t Board::port_a_pins() const
{
    return uint8_t((pia_.port_a() & pia_.ddr_a()) | ~pia_.ddr_a());
}

// CA2: set / reset output (CRA bits 5-3 = 11x), else not driven (high).
bool Board::ca2() const
{
    return (pia_.ctl_a() & 0x30) == 0x30 ? (pia_.ctl_a() & 0x08) != 0 : true;
}

void Board::port_a_out(void* ctx, uint8_t)
{
    Board& b = *static_cast<Board*>(ctx);
    if (b.port_a_changed) b.port_a_changed(b.owner, b.port_a_pins());
}

void Board::port_b_out(void* ctx, uint8_t v)
{
    Board& b = *static_cast<Board*>(ctx);
    if (v == b.dac_) return;
    b.audio_to(b.done_ + b.cpu_.elapsed());
    b.dac_ = v;
    ++b.dac_writes_;
}

void Board::irq_changed(void* ctx)
{
    Board& b = *static_cast<Board*>(ctx);
    b.cpu_.irq(b.pia_.irq());
}

void Board::set_command(uint8_t pins)
{
    pia_.in_a = pins;
}

void Board::set_ca1(bool level)
{
    pia_.ca1(level);
}

void Board::set_volume(uint8_t v)
{
    const float g = 0.5f * (attenuation(v >> 4) + attenuation(v & 0x0f));
    if (g == gain_) return;
    audio_to(done_);
    gain_ = g;
}

void Board::begin_frame(int16_t* audio, int samples, int cycles)
{
    audio_ = audio && samples > 0 ? audio : nullptr;
    samples_ = audio_ ? samples : 0;
    cycles_ = cycles;
    inv_cycles_ = 1.0f / float(cycles);
    out_ = 0;                           // done_ keeps the last frame's overrun
    pos_ = 0;
    acc_ = 0;
}

void Board::run_to(int cycle)
{
    while (done_ < cycle) done_ += cpu_.run(cycle - done_);
}

void Board::end_frame()
{
    run_to(cycles_);
    audio_to(cycles_);
    done_ -= cycles_;                   // the overrun carries into the next frame
    if (done_ < 0) done_ = 0;
    audio_ = nullptr;
}

// Box-filters the DAC level over each output sample, then the volume and
// the high-pass.
void Board::audio_to(int cycle)
{
    if (!audio_) return;
    if (cycle > cycles_) cycle = cycles_;
    const int64_t until = int64_t(cycle) * samples_;
    while (pos_ < until) {
        const int64_t end = int64_t(out_ + 1) * cycles_;
        const int64_t to = until < end ? until : end;
        acc_ += (to - pos_) * dac_;
        pos_ = to;
        if (to == end) {
            const float x = (float(acc_) * inv_cycles_ - 128.0f) * kScale * gain_;
            hp_y_ = hp_a_ * (hp_y_ + x - hp_x_);
            hp_x_ = x;
            float y = hp_y_;
            if (y > 32767.0f) y = 32767.0f;
            if (y < -32768.0f) y = -32768.0f;
            audio_[out_++] = int16_t(y);
            acc_ = 0;
        }
    }
}

} // namespace qix_sound
