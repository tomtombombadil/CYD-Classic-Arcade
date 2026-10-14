// A small raw DEFLATE (RFC 1951) decoder, written for this project (MIT).
// Plain C++, host-tested. Used to unpack deflated members of ROM ZIP files
// (emu/rom/zip.*). Copied from the sister project CYD Classic Games.
#pragma once

#include <cstddef>
#include <cstdint>

namespace inflate {

// Unpacks `in` (raw DEFLATE, no zlib header) into `out`. Returns the bytes
// written, or -1 when the data is bad or `out` is too small.
long raw(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap);

} // namespace inflate
