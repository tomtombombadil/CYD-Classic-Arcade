// CRC-32 (ISO-HDLC / ZIP / PNG polynomial 0xEDB88320), written for this
// project (MIT). Table built at compile time (1 KB, const).
#pragma once

#include <cstddef>
#include <cstdint>

namespace crc32 {

// Continues a CRC: start with crc = 0, feed the data in any number of parts.
uint32_t update(uint32_t crc, const uint8_t* data, size_t len);
inline uint32_t of(const uint8_t* data, size_t len) { return update(0, data, len); }

} // namespace crc32
