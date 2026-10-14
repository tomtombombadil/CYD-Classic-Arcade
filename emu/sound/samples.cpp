// WAV parser and sample player - see samples.h.
#include "samples.h"

namespace samples {

namespace {

uint32_t le32(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
bool tag(const uint8_t* p, const char* t) { return p[0] == t[0] && p[1] == t[1] && p[2] == t[2] && p[3] == t[3]; }

} // namespace

bool parse_wav(const uint8_t* buf, size_t len, Wav& out)
{
    out = Wav{};
    if (!buf || len < 12 || !tag(buf, "RIFF") || !tag(buf + 8, "WAVE")) return false;
    // Walk the chunks; RIFF pads odd-sized chunks to an even length.
    size_t pos = 12;
    bool have_fmt = false;
    uint16_t format = 0, channels = 0, bits = 0, block = 0;
    uint32_t rate = 0;
    while (pos + 8 <= len) {
        const uint8_t* c = buf + pos;
        const uint32_t size = le32(c + 4);
        const size_t body = pos + 8;
        const size_t avail = len - body;
        if (tag(c, "fmt ")) {
            if (size < 16 || size > avail) return false;
            format = le16(c + 8);
            channels = le16(c + 10);
            rate = le32(c + 12);
            block = le16(c + 20);
            bits = le16(c + 22);
            // WAVE_FORMAT_EXTENSIBLE: the real format is the sub-format GUID's first 2 bytes.
            if (format == 0xfffe && size >= 40) format = le16(c + 32);
            have_fmt = true;
        } else if (tag(c, "data")) {
            if (!have_fmt) return false;
            if (format != 1 || (bits != 8 && bits != 16) || channels < 1 || channels > 2) return false;
            if (rate == 0 || block != channels * (bits / 8)) return false;
            // A data chunk that claims more than the file holds (a truncated
            // file) plays what is there.
            const uint32_t bytes = size < avail ? size : uint32_t(avail);
            out.data = c + 8;
            out.frames = bytes / block;
            out.rate = rate;
            out.bits = uint8_t(bits);
            out.channels = uint8_t(channels);
            return out.frames > 0;
        }
        if (size > avail) return false;
        pos = body + size + (size & 1);
    }
    return false;
}

void Player::init(uint32_t out_rate)
{
    out_rate_ = out_rate ? out_rate : 1;
    for (Voice& v : voice_) v = Voice{nullptr, 0, 0, 256, false};
}

void Player::reset()
{
    for (Voice& v : voice_) v.wav = nullptr;
}

void Player::start(int ch, const Wav* w, bool loop)
{
    if (ch < 0 || ch >= kMaxChannels) return;
    Voice& v = voice_[ch];
    if (!w || !w->frames || !w->rate) { v.wav = nullptr; return; }
    v.wav = w;
    v.pos = 0;
    v.step = (uint64_t(w->rate) << 32) / out_rate_;
    v.loop = loop;
}

void Player::stop(int ch)
{
    if (ch >= 0 && ch < kMaxChannels) voice_[ch].wav = nullptr;
}

void Player::set_volume(int ch, int vol)
{
    if (ch >= 0 && ch < kMaxChannels) voice_[ch].volume = vol < 0 ? 0 : vol > 256 ? 256 : vol;
}

// Linear interpolation between neighbouring frames: cheap, and good enough
// for the 11-22 kHz recordings these sets hold. (Downsampling a much higher
// rate would alias; sample sets don't do that.)
void Player::mix(int16_t* out, int n)
{
    for (Voice& v : voice_) {
        if (!v.wav) continue;
        const Wav& w = *v.wav;
        const uint64_t end = uint64_t(w.frames) << 32;
        for (int i = 0; i < n; ++i) {
            if (v.pos >= end) {
                if (!v.loop) { v.wav = nullptr; break; }
                v.pos %= end;
            }
            const uint32_t idx = uint32_t(v.pos >> 32);
            const int frac = int((v.pos >> 17) & 0x7fff);       // 15 bits: the product fits 32
            const int a = w.at(idx);
            // The frame after the last is the first again for a loop, silence otherwise.
            const int b = idx + 1 < w.frames ? w.at(idx + 1) : (v.loop ? w.at(0) : 0);
            const int s = a + (((b - a) * frac) >> 15);
            int32_t m = out[i] + ((s * v.volume) >> 8);
            if (m > 32767) m = 32767;
            if (m < -32768) m = -32768;
            out[i] = int16_t(m);
            v.pos += v.step;
        }
    }
}

} // namespace samples
