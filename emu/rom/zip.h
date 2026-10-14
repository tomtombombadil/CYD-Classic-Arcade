// ZIP archive reader on a memory buffer, written for this project (MIT).
// Reads the central directory; members may be stored or deflated (unpacked
// with emu/util/inflate). No ZIP64, encryption or multi-disk archives -
// ROM sets never need them.
#pragma once

#include <cstddef>
#include <cstdint>

namespace zip {

struct Entry {
    const char* name;           // not NUL-terminated; may contain folders
    uint16_t    name_len;
    uint16_t    method;         // 0 stored, 8 deflate
    uint32_t    crc;            // as recorded in the directory
    uint32_t    comp_size;
    uint32_t    size;           // uncompressed
    uint32_t    local_offset;   // of the local header
};

class Reader {
public:
    // False when `data` is not a ZIP (or its directory is damaged).
    bool open(const uint8_t* data, size_t len);
    int  count() const { return count_; }

    // Iteration: uint32_t it = begin(); Entry e; while (next(it, e)) ...
    uint32_t begin() const { return cd_offset_; }
    bool next(uint32_t& it, Entry& e) const;

    // Unpacks a member into `out` (e.size bytes) and checks its CRC.
    bool extract(const Entry& e, uint8_t* out) const;

private:
    const uint8_t* data_ = nullptr;
    size_t   len_ = 0;
    uint32_t cd_offset_ = 0, cd_end_ = 0;
    int      count_ = 0;
};

// True when the buffer starts like a ZIP file.
bool looks_like_zip(const uint8_t* data, size_t len);

} // namespace zip
