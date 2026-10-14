// CRC-32 - see crc32.h.
#include "crc32.h"

namespace crc32 {

namespace {

struct Table {
    uint32_t t[256];
    constexpr Table() : t()
    {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
    }
};
constexpr Table kTable{};

} // namespace

uint32_t update(uint32_t crc, const uint8_t* data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) crc = kTable.t[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

} // namespace crc32
