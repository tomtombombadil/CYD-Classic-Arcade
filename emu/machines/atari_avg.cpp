// Atari AVG (Tempest, Star Wars, Battlezone) - see atari_avg.h.
#include "atari_avg.h"

namespace atari_avg {

void Avg::init(read_fn rd, line_fn line, void* ctx, uint16_t base, int32_t cx, int32_t cy, Variant variant)
{
    variant_ = variant;
    rd_ = rd;
    line_ = line;
    ctx_ = ctx;
    base_ = base;
    cx_ = cx;
    cy_ = cy;
    x_ = cx;
    y_ = cy;
    reset();
}

void Avg::reset()
{
    scale_ = bin_scale_ = 0;
    color_ = 0;
    halted_ = true;
}

void Avg::set_stat(uint16_t w0)
{
    if (variant_ == Variant::StarWars) {    // STAT: colour + intensity
        color_ = (w0 >> 8) & 0xf;
        intensity_ = w0 & 0xff;
        // A vector's brightness: z's bits 2 / 1 / 0 through the 12k / 24k /
        // 47k resistor DAC (full scale 1 + 1/2 + 12/47) times this 8-bit
        // intensity - for each z once here, not at every vector.
        static constexpr int kDac[8] = {0, 2051, 4017, 6069, 8034, 10086, 12052, 14103};  // x 14103
        for (int z = 0; z < 8; ++z) level_[z] = uint8_t((kDac[z] * intensity_ + 14103 / 2) / 14103);
    } else if (w0 & 0x800) {                // STAT: colour
        color_ = w0 & 0xf;
    } else {                                // STAT: intensity
        intensity_ = (w0 >> 4) & 0xf;
    }
}

namespace {

// The line function as a sink.
struct CallLine {
    line_fn line;
    void*   ctx;
    void operator()(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int color, int level) const
    {
        line(ctx, x0, y0, x1, y1, color, level);
    }
};

} // namespace

int Avg::run(int max_ops)
{
    CallLine sink{line_, ctx_};
    switch (variant_) {
    case Variant::StarWars:   return run_lines<Variant::StarWars>(sink, max_ops);
    case Variant::Battlezone: return run_lines<Variant::Battlezone>(sink, max_ops);
    default:                  return run_lines<Variant::Tempest>(sink, max_ops);
    }
}

} // namespace atari_avg
