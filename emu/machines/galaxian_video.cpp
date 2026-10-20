// Galaxian video - see galaxian_video.h.
#include "galaxian_video.h"

#include <cstring>

namespace galaxian_video {

namespace {

// Palette PROM bits -> 8-bit levels: red and green 1K / 470 / 220 ohm, blue
// 470 / 220 ohm, into a 470 ohm load (weights = conductance shares, the
// brightest channel scaled to 224 as the monitor's input allows).
constexpr uint8_t kRGWeights[3] = {29, 62, 133};
constexpr uint8_t kBWeights[2] = {69, 148};
// Stars: two bits a channel through 150 / 100 ohm into the same load.
constexpr uint8_t kStarLevels[4] = {0, 194, 214, 255};
// The Konami boards' background: red 0x55, green 0x47, blue 0x55 a bit.
constexpr uint8_t kBgLevels[3] = {0x55, 0x47, 0x55};

uint16_t rgb565(int r, int g, int b, bool swap)
{
    const uint16_t px = uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return swap ? uint16_t((px >> 8) | (px << 8)) : px;
}

// Pixel (x along the scanline, line y) of a tile / sprite: plane 0 (the
// pixel's high bit) in the first 2 KB, plane 1 2 KB on; bit 7 = x 0.
// Sprites: x 8-15 8 bytes on, lines 8-15 16 bytes on.
int tile_pixel(const uint8_t* gfx, int code, int x, int y)
{
    const int at = code * 8 + y, bit = 7 - x;
    return (((gfx[at] >> bit) & 1) << 1) | ((gfx[0x800 + at] >> bit) & 1);
}

int sprite_pixel(const uint8_t* gfx, int code, int x, int y)
{
    const int at = code * 32 + (x & 8) + ((y & 8) << 1) + (y & 7), bit = 7 - (x & 7);
    return (((gfx[at] >> bit) & 1) << 1) | ((gfx[0x800 + at] >> bit) & 1);
}

} // namespace

void Video::init(const uint8_t* prom, const uint8_t* gfx, const Options& options, bool swap)
{
    opt_ = options;
    for (int e = 0; e < 32; ++e) {
        const uint8_t v = prom[e];
        const int r = kRGWeights[0] * (v & 1) + kRGWeights[1] * ((v >> 1) & 1) + kRGWeights[2] * ((v >> 2) & 1);
        const int g = kRGWeights[0] * ((v >> 3) & 1) + kRGWeights[1] * ((v >> 4) & 1) + kRGWeights[2] * ((v >> 5) & 1);
        const int b = kBWeights[0] * ((v >> 6) & 1) + kBWeights[1] * ((v >> 7) & 1);
        pen_[e] = rgb565(r, g, b, swap);
    }
    for (int c = 0; c < 64; ++c) {
        // Red: bits 5 (150 ohm) and 4 (100 ohm), green 3 / 2, blue 1 / 0.
        const int r = kStarLevels[((c >> 4) & 1) * 2 + ((c >> 5) & 1)];
        const int g = kStarLevels[((c >> 2) & 1) * 2 + ((c >> 3) & 1)];
        const int b = kStarLevels[(c & 1) * 2 + ((c >> 1) & 1)];
        star_rgb_[c] = rgb565(r, g, b, swap);
    }
    for (int k = 0; k < 8; ++k)
        bg_[k] = options.background ? rgb565(kBgLevels[0] * (k & 1), kBgLevels[1] * ((k >> 1) & 1),
                                             kBgLevels[2] * ((k >> 2) & 1), swap)
                                    : 0;
    shell_ = rgb565(255, 255, 255, swap);
    missile_ = rgb565(255, 255, 0, swap);

    for (int code = 0; code < 256; ++code)
        for (int x = 0; x < 8; ++x) {
            uint16_t bits = 0;
            for (int y = 0; y < 8; ++y) bits |= uint16_t(tile_pixel(gfx, code, x, y) << (2 * y));
            tile_cols_[code * 8 + x] = bits;
        }
    for (int code = 0; code < 64; ++code)
        for (int x = 0; x < 16; ++x) {
            uint32_t bits = 0;
            for (int y = 0; y < 16; ++y) bits |= uint32_t(sprite_pixel(gfx, code, x, y)) << (2 * y);
            sprite_cols_[code * 16 + x] = bits;
        }

    // The LFSR's states that light a star, from state 0 (the period's start).
    star_states_n_ = 0;
    if (options.stars) {
        uint32_t s = 0;
        for (uint32_t i = 0; i < kStarPeriod; ++i) {
            if ((s & 0x1fe01) == 0x1fe00 && star_states_n_ < kMaxStars)
                star_states_[star_states_n_++] = i | (((~s >> 3) & 0x3f) << 18);
            s = star_step(s);
        }
    }
}

void Video::capture(VideoState& vs, const uint8_t* vram, const uint8_t* obj, uint8_t background, bool stars_on,
                    uint32_t origin) const
{
    std::memcpy(vs.vram, vram, sizeof vs.vram);
    std::memcpy(vs.obj, obj, sizeof vs.obj);
    vs.background = background & 7;

    // Line L of the picture reads the LFSR states origin + 512 L on (two a
    // pixel), so a state shows at offset (state - origin) mod period: line
    // offset / 512, pixel (offset % 512) / 2. Counted by display line, then
    // placed.
    uint8_t state_of[kMaxStars], line_of[kMaxStars], x_of[kMaxStars];
    int n = 0;
    const uint32_t start = origin % kStarPeriod;
    uint16_t count[kScreenH] = {};
    if (stars_on)
        for (int k = 0; k < star_states_n_; ++k) {
            const uint32_t state = star_states_[k] & 0x1ffff;
            const uint32_t q = (state + kStarPeriod - start) % kStarPeriod;
            const int line = int(q >> 9), x = int((q & 511) >> 1);
            if (line < kFirstLine || line > kLastLine || !((line ^ (x >> 3)) & 1)) continue;
            state_of[n] = uint8_t(k);
            line_of[n] = uint8_t(line);
            x_of[n++] = uint8_t(x);
            ++count[x];
        }
    vs.star_count = uint16_t(n);
    vs.star_at[0] = 0;
    for (int y = 0; y < kScreenH; ++y) vs.star_at[y + 1] = uint16_t(vs.star_at[y] + count[y]);
    uint16_t fill[kScreenH];
    std::memcpy(fill, vs.star_at, sizeof fill);
    // In LFSR order, so of two states on one pixel the second (it lights
    // two of the pixel's three thirds) is drawn last.
    for (int i = 0; i < n; ++i) {
        const uint16_t at = fill[x_of[i]]++;
        vs.star_x[at] = uint8_t(display_x(line_of[i]));
        vs.star_pen[at] = uint8_t(star_states_[state_of[i]] >> 18);
    }
}

void Video::render_line(const VideoState& vs, int y, uint16_t* out) const
{
    // Background and stars.
    const uint16_t bg = bg_[vs.background];
    for (int x = 0; x < kScreenW; ++x) out[x] = bg;
    for (int k = vs.star_at[y]; k < vs.star_at[y + 1]; ++k) out[vs.star_x[k]] = star_rgb_[vs.star_pen[k]];

    // Tiles: display line y is one tile column, with its scroll and colour.
    const int col = y >> 3, fx = y & 7;
    const uint8_t scroll = vs.obj[col * 2];
    const uint16_t* pens = pen_ + (vs.obj[col * 2 + 1] & 7) * 4;
    for (int line = kFirstLine; line <= kLastLine;) {
        const int ty = (line + scroll) & 0xff;
        const int fy = ty & 7;
        int n = 8 - fy;
        if (n > kLastLine + 1 - line) n = kLastLine + 1 - line;
        uint32_t bits = tile_cols_[vs.vram[(ty >> 3) * 32 + col] * 8 + fx] >> (2 * fy);
        if (bits) {
            uint16_t* o = out + display_x(line);
            for (int i = 0; i < n; ++i, bits >>= 2)
                if (bits & 3) o[-i] = pens[bits & 3];
        }
        line += n;
    }

    // Sprites, 7 first so sprite 0 ends on top.
    if (y >= kSpriteClip)
        for (int s = 7; s >= 0; --s) {
            const uint8_t* sp = vs.obj + 0x40 + s * 4;
            int gx = y - uint8_t(sp[3] + 1);
            if (gx < 0 || gx >= 16) continue;
            if (sp[1] & 0x40) gx = 15 - gx;
            uint32_t bits = sprite_cols_[(sp[1] & 0x3f) * 16 + gx];
            if (!bits) continue;
            const bool flipy = sp[1] & 0x80;
            const uint16_t* sp_pens = pen_ + (sp[2] & 7) * 4;
            const int top = sprite_line(sp[0], s);
            for (int gy = 0; gy < 16; ++gy) {
                const int line = top + gy;
                if (line < kFirstLine || line > kLastLine) continue;
                const int p = int(bits >> (2 * (flipy ? 15 - gy : gy))) & 3;
                if (p) out[display_x(line)] = sp_pens[p];
            }
        }

    // Bullets: 4 pixels ending one before 255 - X; of the shells (0-6) only
    // the last one on a line shows, then the missile (7).
    if (opt_.bullets) {
        const uint8_t* b = vs.obj + 0x60;
        for (int n = 0; n < 8; ++n) {
            const int x = 255 - b[n * 4 + 3];
            if (y < x - 4 || y >= x) continue;
            const int line = bullet_line(b[n * 4 + 1], n);
            if (line < kFirstLine || line > kLastLine) continue;
            bool hidden = false;
            for (int m = n + 1; m < 7 && n < 7; ++m)
                if (bullet_line(b[m * 4 + 1], m) == line) hidden = true;
            if (!hidden) out[display_x(line)] = n == 7 ? missile_ : shell_;
        }
    }
}

} // namespace galaxian_video
