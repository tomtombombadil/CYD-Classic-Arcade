// Galaga machine - see galaga.h.
//
// Galaga's part of the memory map (the rest: namco_galaga_common.cpp):
//   6800-6807  read: DIP switches, bit 0 = bank B switch A2-A0, bit 1 =
//              bank A's (mirrored through 6FFF where A5-A4 = 0)
//   A000-A007  write: video latch, bit = A2-A0 (mirrored through A7FF):
//              0-2 star scroll speed, 3 / 4 star sets, 5 stars on
//              (STARCLR), 7 flip screen
//   06XX chips: 0 = 51XX, 3 = 54XX
#include "galaga.h"

#include <cstring>

namespace galaga {

namespace {

constexpr romset::Region kRegions[kRegionCount] = {
    {"maincpu", 0x4000},
    {"sub",     0x1000},
    {"sub2",    0x1000},
    {"gfx1",    0x1000},
    {"gfx2",    0x2000},
    {"proms",   0x0220},
    {"namco",   0x0200},
};

// CRCs as in MAME's "galaga" set (older sets name the files gg1-1b.3p etc.).
// The custom chips' own programs (51xx.bin, 54xx.bin) aren't needed.
constexpr romset::Rom kRoms[] = {
    {kRegionMain,    0x0000, 0x1000, 0xab036c9f, "gg1_1b.3p", false},
    {kRegionMain,    0x1000, 0x1000, 0xd9232240, "gg1_2b.3m", false},
    {kRegionMain,    0x2000, 0x1000, 0x753ce503, "gg1_3.2m", false},
    {kRegionMain,    0x3000, 0x1000, 0x499fcc76, "gg1_4b.2l", false},
    {kRegionSub,     0x0000, 0x1000, 0xbb5caae3, "gg1_5b.3f", false},
    {kRegionSound,   0x0000, 0x1000, 0xd016686b, "gg1_7b.2c", false},
    {kRegionChars,   0x0000, 0x1000, 0x58b2f47c, "gg1_9.4l", false},
    {kRegionSprites, 0x0000, 0x1000, 0xad447c80, "gg1_11.4d", false},
    {kRegionSprites, 0x1000, 0x1000, 0xdd6f1afc, "gg1_10.4f", false},
    {kRegionProms,   0x0000, 0x0020, 0x54603c6b, "prom-5.5n", false},
    {kRegionProms,   0x0020, 0x0100, 0x59b6edab, "prom-4.2n", false},
    {kRegionProms,   0x0120, 0x0100, 0x4a04bb6b, "prom-3.1c", false},
    {kRegionWave,    0x0000, 0x0100, 0x7a2815b4, "prom-1.1d", false},
    {kRegionWave,    0x0100, 0x0100, 0x77245b66, "prom-2.5c", true},     // timing, unused
};

// Star colours (BBGGRR) through the 05XX's own resistors: red and green
// 470 / 220 ohm with a 1K pull-down, blue 470 / 220 ohm.
constexpr uint8_t kStarRG[4] = {0, 71, 151, 222};
constexpr uint8_t kStarB[4] = {0, 81, 174, 255};

// Idle loops (see namco_galaga::Speedup), found by profiling and read in
// the program; none is entered other than from its top. The polling loops
// are checked at their first instruction (the RAM read); "spins" = the
// value that keeps the loop going.
//   sub 05B1-05B6    LD SP,9100 / JP 05B1: the sub CPU's main loop, all
//                    its work is in the VBLANK interrupt
//   sub 05E7-05EC    LD A,(92D6) / DEC A / JR Z: waits for the main CPU's
//                    sprite copy (spins while 92D6 = 1)
//   sound 00B9-00BA  JR 00B9: the sound CPU's main loop (work in the NMIs)
//   main 038D-0392   LD A,(9201) / DEC A / JR Z: attract mode, waits for
//                    the sub / sound CPU (spins while 9201 = 1)
//   main 0414-0417   LD A,(HL) / AND A / JR NZ with HL = 92AF (set just
//   main 1337-133A   before): a delay the VBLANK handler counts down
//                    (spins while 92AF != 0)
//   main 0850-0855   LD A,(92D7) / DEC A / JR Z: waits for the sub CPU's
//                    sprite copy (spins while 92D7 = 1)
//   main 134C-1351   LD A,(9287) / AND A / JR NZ: a timer the VBLANK
//                    handler counts down (spins while 9287 != 0)
//   main 04A4-04C4   between stages: spins while 901D = 0 and 92AF != 0
//                    (LD A,(901D) / AND A / JR Z,04C1 / LD A,(HL) with HL =
//                    92AF / AND A / JR NZ,04A4); checked at 04A4 and 04C1
//   main 045E-0465   the main CPU's background loop in play: CALL 0728
//   main 0728-07AD   (add the 16 score counters 9290-929F to the score
//   main 080B-0826   digits, copy a higher score to the high score, award
//                    a bonus fighter when the score reaches 983E), CALL
//                    080B (leave on 9008 / 92A7 / 9213), JR 045E. Its
//                    inputs: 83E0-83FF (score digits), 9290-929F, 9840
//                    (player), 983E, 9008, 92A7, 9213; every pass with work
//                    changes one of them, a pass without writes nothing. So
//                    once they have stayed the same for 6000 cycles (a pass
//                    takes 2000-2250 without work, NMIs aside: two whole
//                    passes since the change) it only spins until they change - the
//                    sub CPU adds score, the VBLANK handler moves the game
//                    on. The NMI handler (0066) only moves 06XX bytes. In
//                    0728-07AD / 080B-0826 the stack must hold the return
//                    to the loop (0728 is called from elsewhere too); RST 10
//                    (0010-0014) inside it is stepped through.
using namco_galaga::Idle;
template <uint16_t kTop, uint16_t kAddr, int kSpinValue>
Idle while_equal(namco_galaga::Board& b, uint16_t pc)
{
    if (pc != kTop) return Idle::kStep;
    return b.peek(kAddr) == kSpinValue ? Idle::kYes : Idle::kNo;
}
template <uint16_t kTop, uint16_t kAddr>
Idle while_nonzero(namco_galaga::Board& b, uint16_t pc)
{
    if (pc != kTop) return Idle::kStep;
    return b.peek(kAddr) ? Idle::kYes : Idle::kNo;
}

Idle stage_wait(namco_galaga::Board& b, uint16_t pc)
{
    if (pc != 0x04a4 && pc != 0x04c1) return Idle::kStep;
    return !b.peek(0x901d) && b.peek(0x92af) ? Idle::kYes : Idle::kNo;
}

constexpr namco_galaga::Board::Range kLoopInputs[] = {
    {0x83e0, 0x83ff}, {0x9290, 0x929f}, {0x9840, 0x9840}, {0x983e, 0x983e},
    {0x9008, 0x9008}, {0x92a7, 0x92a7}, {0x9213, 0x9213},
};
constexpr int kLoopSteadyCycles = 6000;

Idle background_loop(namco_galaga::Board& b, uint16_t pc)
{
    if (pc <= 0x0014) return Idle::kStep;                       // in RST 10
    if (pc >= 0x0728) {
        const uint16_t sp = b.cpu(namco_galaga::kMain).sp.w;
        if (sp < 0x8000 || sp >= 0x9bff) return Idle::kNo;
        const uint16_t ret = uint16_t(b.peek(sp) | (b.peek(uint16_t(sp + 1)) << 8));
        if (ret != (pc < 0x080b ? 0x0461 : 0x0464)) return Idle::kNo;
    }
    return b.steady(namco_galaga::kMain, kLoopInputs, int(sizeof kLoopInputs / sizeof kLoopInputs[0]),
                    kLoopSteadyCycles)
               ? Idle::kYes
               : Idle::kNo;
}

constexpr namco_galaga::Speedup kSpeedups[] = {
    {namco_galaga::kSub, 0x05b1, 0x05b6, nullptr, 10},
    {namco_galaga::kSub, 0x05e7, 0x05ec, while_equal<0x05e7, 0x92d6, 1>, 10},
    {namco_galaga::kSound, 0x00b9, 0x00ba, nullptr, 12},
    {namco_galaga::kMain, 0x038d, 0x0392, while_equal<0x038d, 0x9201, 1>, 10},
    {namco_galaga::kMain, 0x0414, 0x0417, while_nonzero<0x0414, 0x92af>, 8},
    {namco_galaga::kMain, 0x1337, 0x133a, while_nonzero<0x1337, 0x92af>, 8},
    {namco_galaga::kMain, 0x0850, 0x0855, while_equal<0x0850, 0x92d7, 1>, 10},
    {namco_galaga::kMain, 0x134c, 0x1351, while_nonzero<0x134c, 0x9287>, 8},
    {namco_galaga::kMain, 0x04a4, 0x04c4, stage_wait, 8},
    {namco_galaga::kMain, 0x045e, 0x0465, background_loop, 5},
    {namco_galaga::kMain, 0x0728, 0x07ad, background_loop, 5},
    {namco_galaga::kMain, 0x080b, 0x0826, background_loop, 5},
    {namco_galaga::kMain, 0x0010, 0x0014, background_loop, 5},
};

} // namespace

const romset::Set kRomSet = {
    "galaga", nullptr, "Galaga (Namco rev. B)", kRegions, kRegionCount, kRoms, int(sizeof kRoms / sizeof kRoms[0]),
};

void Machine::init(const Roms& roms, uint32_t audio_rate, bool swap)
{
    roms_ = roms;
    BoardRoms b{};
    b.cpu[namco_galaga::kMain] = roms.main;
    b.cpu[namco_galaga::kSub] = roms.sub;
    b.cpu[namco_galaga::kSound] = roms.sound;
    b.cpu_size[namco_galaga::kMain] = 0x4000;
    b.cpu_size[namco_galaga::kSub] = 0x1000;
    b.cpu_size[namco_galaga::kSound] = 0x1000;
    b.wave = roms.wave;
    noise_.init(audio_rate);
    stars_.init();

    // Colour lookup: characters use palette entries 16-31, sprites 0-15;
    // a lookup value of 15 is transparent in both layers.
    const uint8_t* pal = roms.proms;
    for (int c = 0; c < 64; ++c) {
        char_opaque_[c] = sprite_opaque_[c] = 0;
        for (int p = 0; p < 4; ++p) {
            const uint8_t ce = roms.proms[0x20 + c * 4 + p] & 0x0f;
            const uint8_t se = roms.proms[0x120 + c * 4 + p] & 0x0f;
            char_pen_[c * 4 + p] = namco_galaga::prom_rgb565(pal[0x10 | ce], swap);
            sprite_pen_[c * 4 + p] = namco_galaga::prom_rgb565(pal[se], swap);
            if (ce != 0x0f) char_opaque_[c] |= uint8_t(1 << p);
            if (se != 0x0f) sprite_opaque_[c] |= uint8_t(1 << p);
        }
    }
    for (int c = 0; c < 64; ++c)
        star_pen_[c] = namco_galaga::rgb565(kStarRG[c & 3], kStarRG[(c >> 2) & 3], kStarB[(c >> 4) & 3], swap);

    namco_galaga::decode_chars_2bpp(roms.chars, 128, char_rows_);
    namco_galaga::decode_sprites(roms.sprites, 128, sprite_rows_);
    board_init(b, audio_rate, kSpeedups, int(sizeof kSpeedups / sizeof kSpeedups[0]));
    reset();
}

void Machine::reset()
{
    board_reset();
    video_latch_ = 0;
    stars_.reset();
    stars_.set_controls(0, 0, 2, false);
}

uint8_t Machine::io_read(int, uint16_t a)
{
    if ((a & 0xf830) == 0x6800) {
        const int bit = a & 7;
        return uint8_t(((dips_b_ >> bit) & 1) | (((dips_a_ >> bit) & 1) << 1));
    }
    return 0xff;
}

void Machine::io_write(int, uint16_t a, uint8_t v)
{
    if ((a & 0xf800) == 0xa000) {
        const uint8_t bit = uint8_t(1 << (a & 7));
        video_latch_ = (v & 1) ? uint8_t(video_latch_ | bit) : uint8_t(video_latch_ & ~bit);
    }
}

void Machine::chip_write(int chip, uint8_t v)
{
    if (chip == 3) {
        audio_to(now(namco_galaga::kMain));
        noise_.write(v);
    }
}

void Machine::chips_reset() { noise_.reset(); }

void Machine::mix(int16_t* out, int n) { noise_.mix(out, n); }

void Machine::frame_end()
{
    // The 05XX takes its inputs at the end of VBLANK.
    stars_.set_controls(video_latch_ & 7, (video_latch_ >> 3) & 1, uint8_t(((video_latch_ >> 4) & 1) | 2),
                        (video_latch_ & 0x20) != 0);
}

void Machine::vblank()
{
    VideoState& vs = video_;
    std::memcpy(vs.vram, vram_, 0x400);
    std::memcpy(vs.cram, vram_ + 0x400, 0x400);

    // Sprites: 64 of them, codes / colours at 8B80, position at 9380,
    // flags at 9B80 (two bytes each). x is the hardware's (the display
    // line), 9 bits; y counts up from the bottom of the hardware screen.
    vs.sprite_count = 0;
    for (int s = 0; s < kSprites; ++s) {
        const uint8_t* r1 = ram_[0] + 0x380 + 2 * s;
        const uint8_t* r2 = ram_[1] + 0x380 + 2 * s;
        const uint8_t* r3 = ram_[2] + 0x380 + 2 * s;
        const uint8_t flags = r3[0] & 0x0f;
        const int wide = (flags & namco_galaga::kWide) ? 1 : 0, tall = (flags & namco_galaga::kTall) ? 1 : 0;
        const int x = r2[1] - 40 + 0x100 * (r3[1] & 3);
        int y = 256 - r2[0] + 1 - 16 * tall;
        y = (y & 0xff) - 32;
        if (x >= kScreenH || x + (16 << wide) <= 0) continue;
        vs.sprite[vs.sprite_count++] = {int16_t(x), int16_t(y), uint8_t(r1[0] & 0x7f), uint8_t(r1[1] & 0x3f), flags};
    }

    // Stars: the field's x runs down the display from line 16, its y from
    // the right edge to the left. Sorted by line (a counting sort).
    starfield05::Star list[kMaxStars];
    const int n = stars_.frame(list);
    uint8_t count[kScreenH + 1] = {};
    for (int i = 0; i < n; ++i) ++count[16 + list[i].x];
    int at = 0;
    for (int line = 0; line <= kScreenH; ++line) {
        vs.star_first[line] = uint8_t(at);
        at += count[line];
        count[line] = vs.star_first[line];      // next free slot on the line
    }
    for (int i = 0; i < n; ++i) {
        const int line = 16 + list[i].x;
        vs.star[count[line]++] = {uint16_t(line), uint8_t(kScreenW - 1 - list[i].y), list[i].colour};
    }
}

void Machine::render_line(const VideoState& vs, int y, uint16_t* out) const
{
    // Black, then the stars, the sprites and the characters on top.
    for (int x = 0; x < kScreenW; ++x) out[x] = 0;
    for (int i = vs.star_first[y]; i < vs.star_first[y + 1]; ++i) out[vs.star[i].x] = star_pen_[vs.star[i].colour];

    for (int s = 0; s < vs.sprite_count; ++s) {
        const namco_galaga::Sprite& sp = vs.sprite[s];
        namco_galaga::draw_sprite_line(sp, y, sprite_rows_, 128, sprite_pen_ + sp.colour * 4,
                                       sprite_opaque_[sp.colour], out);
    }

    int off, step;
    namco_galaga::tile_row(y >> 3, off, step);
    const int ty = y & 7;
    for (int col = 0; col < 28; ++col, off += step) {
        const uint16_t bits = char_rows_[(vs.vram[off] & 0x7f) * 8 + ty];
        const int colour = vs.cram[off] & 0x3f;
        const uint8_t opaque = char_opaque_[colour];
        const uint16_t* pens = char_pen_ + colour * 4;
        uint16_t* o = out + col * 8;
        for (int x = 0; x < 8; ++x) {
            const int p = (bits >> (2 * x)) & 3;
            if (opaque & (1 << p)) o[x] = pens[p];
        }
    }
}

} // namespace galaga
