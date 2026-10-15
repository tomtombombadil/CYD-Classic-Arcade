// The Williams sound board - see williams_sound.h.
#include "williams_sound.h"

#include <cmath>

namespace williams_sound {

namespace {

// DAC level (0-255, its middle at 128) to output: full swing = +-10,900
// (with the DC blocker the loudest sounds peak at ~21,000).
constexpr float kScale = 85.0f;

} // namespace

void Board::init(const uint8_t* rom, uint32_t rom_size, uint32_t audio_rate)
{
    rom_ = rom;
    rom_mask_ = rom_size - 1;
    cpu_.ctx = this;
    cpu_.mem_read = mem_read;
    cpu_.mem_write = mem_write;
    cpu_.map_ram(0x0000, 0x400, ram_);
    // ROM in E000-FFFF, seen as often as it fits.
    for (uint32_t a = 0xe000; a < 0x10000; a += m6800::Cpu::kPageSize)
        cpu_.map_read(uint16_t(a), m6800::Cpu::kPageSize, rom + (a & rom_mask_));
    pia_.ctx = this;
    pia_.out_a = dac_write;
    pia_.irq_changed = irq_changed;
    // DC blocker at ~10 Hz.
    hp_a_ = float(std::exp(-2.0 * 3.14159265 * 10.0 / double(audio_rate)));
    reset();
}

void Board::reset()
{
    pia_.reset();
    pia_.in_b = 0xff;                   // no command
    pia_.cb1(false);
    cpu_.irq(false);
    cpu_.reset();
}

uint8_t Board::mem_read(void* ctx, uint16_t addr)
{
    Board& b = *static_cast<Board*>(ctx);
    if ((addr & 0x7ffc) == 0x0400) return b.pia_.read(addr & 3);
    return 0;
}

// The DAC is written tens of thousands of times a second: port A's data
// goes straight to it when the port is all output.
void Board::mem_write(void* ctx, uint16_t addr, uint8_t v)
{
    Board& b = *static_cast<Board*>(ctx);
    if ((addr & 0x7ffc) != 0x0400) return;
    if ((addr & 3) == 0 && b.pia_.a_is_output_data()) {
        b.pia_.set_out_a_reg(v);
        b.dac_set(v);
    } else {
        b.pia_.write(addr & 3, v);
    }
}

void Board::dac_write(void* ctx, uint8_t v)
{
    static_cast<Board*>(ctx)->dac_set(v);
}

void Board::dac_set(uint8_t v)
{
    if (v == dac_) return;
    audio_to(done_ + cpu_.elapsed());
    dac_ = v;
    ++dac_writes_;
}

void Board::irq_changed(void* ctx)
{
    Board& b = *static_cast<Board*>(ctx);
    b.cpu_.irq(b.pia_.irq());
}

void Board::command(uint8_t cmd)
{
    const uint8_t v = uint8_t(cmd | 0xc0);
    pia_.in_b = v;
    pia_.cb1(v != 0xff);
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

// Box-filters the DAC level over each output sample, then the DC blocker.
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
            const float x = (float(acc_) * inv_cycles_ - 128.0f) * kScale;
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

} // namespace williams_sound
