// PNG and WAV file writers for the host runner (MIT, written for this
// project). The PNG is RGB 8-bit with stored (uncompressed) DEFLATE blocks:
// bigger files, no compressor needed.
#pragma once

#include <cstdint>
#include <cstdio>

namespace writers {

// `rgb565`: w x h pixels, row by row.
bool write_png_rgb565(const char* path, const uint16_t* rgb565, int w, int h);

// A mono 16-bit WAV written as samples arrive; close() fixes the sizes.
class Wav {
public:
    bool open(const char* path, uint32_t rate);
    void write(const int16_t* samples, int n);
    void close();
    ~Wav() { close(); }

private:
    FILE*    f_ = nullptr;
    uint32_t samples_ = 0;
};

} // namespace writers
