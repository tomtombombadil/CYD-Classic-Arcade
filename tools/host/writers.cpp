// PNG / WAV writers - see writers.h.
#include "writers.h"

#include "../../emu/rom/crc32.h"

#include <vector>

namespace writers {

namespace {

void put32be(std::vector<uint8_t>& v, uint32_t x)
{
    v.push_back(uint8_t(x >> 24));
    v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}

void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> c;
    put32be(c, uint32_t(data.size()));
    c.insert(c.end(), type, type + 4);
    c.insert(c.end(), data.begin(), data.end());
    put32be(c, crc32::of(c.data() + 4, c.size() - 4));
    std::fwrite(c.data(), 1, c.size(), f);
}

void put16le(FILE* f, uint16_t x) { std::fputc(x & 0xff, f); std::fputc(x >> 8, f); }
void put32le(FILE* f, uint32_t x) { put16le(f, uint16_t(x)); put16le(f, uint16_t(x >> 16)); }

} // namespace

bool write_png_rgb565(const char* path, const uint16_t* px, int w, int h)
{
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::fwrite(kSig, 1, 8, f);

    std::vector<uint8_t> ihdr;
    put32be(ihdr, uint32_t(w));
    put32be(ihdr, uint32_t(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});     // 8-bit RGB, no interlace
    chunk(f, "IHDR", ihdr);

    std::vector<uint8_t> raw;
    raw.reserve(size_t(h) * (1 + 3 * size_t(w)));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);                          // filter: none
        for (int x = 0; x < w; ++x) {
            const uint16_t p = px[y * w + x];
            const int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
            raw.push_back(uint8_t((r << 3) | (r >> 2)));
            raw.push_back(uint8_t((g << 2) | (g >> 4)));
            raw.push_back(uint8_t((b << 3) | (b >> 2)));
        }
    }
    // zlib stream of stored blocks.
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    do {
        const size_t n = raw.size() - pos < 65535 ? raw.size() - pos : 65535;
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(uint8_t(n));
        z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n));
        z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + long(pos), raw.begin() + long(pos + n));
        pos += n;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;                         // Adler-32
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    put32be(z, (b << 16) | a);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    return std::fclose(f) == 0;
}

bool Wav::open(const char* path, uint32_t rate)
{
    close();
    f_ = std::fopen(path, "wb");
    if (!f_) return false;
    samples_ = 0;
    std::fwrite("RIFF", 1, 4, f_);
    put32le(f_, 36);
    std::fwrite("WAVEfmt ", 1, 8, f_);
    put32le(f_, 16);
    put16le(f_, 1);             // PCM
    put16le(f_, 1);             // mono
    put32le(f_, rate);
    put32le(f_, rate * 2);
    put16le(f_, 2);
    put16le(f_, 16);
    std::fwrite("data", 1, 4, f_);
    put32le(f_, 0);
    return true;
}

void Wav::write(const int16_t* s, int n)
{
    if (!f_) return;
    for (int i = 0; i < n; ++i) put16le(f_, uint16_t(s[i]));
    samples_ += uint32_t(n);
}

void Wav::close()
{
    if (!f_) return;
    std::fseek(f_, 4, SEEK_SET);
    put32le(f_, 36 + samples_ * 2);
    std::fseek(f_, 40, SEEK_SET);
    put32le(f_, samples_ * 2);
    std::fclose(f_);
    f_ = nullptr;
}

} // namespace writers
