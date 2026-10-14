// ZIP archive reader - see zip.h. Layout facts from PKWARE's APPNOTE.TXT.
#include "zip.h"

#include "crc32.h"
#include "../util/inflate.h"

#include <cstring>

namespace zip {

namespace {

constexpr uint32_t kLocalSig = 0x04034b50;
constexpr uint32_t kCentralSig = 0x02014b50;
constexpr uint32_t kEndSig = 0x06054b50;
constexpr size_t   kEndSize = 22;      // end-of-central-directory record without comment

uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }

} // namespace

bool looks_like_zip(const uint8_t* data, size_t len)
{
    return len >= 4 && (u32(data) == kLocalSig || u32(data) == kEndSig);
}

bool Reader::open(const uint8_t* data, size_t len)
{
    data_ = data;
    len_ = len;
    count_ = 0;
    if (len < kEndSize) return false;
    // The end record sits at the end, followed by a comment of up to 64 KB.
    size_t pos = len - kEndSize;
    const size_t stop = len > kEndSize + 0xffff ? len - kEndSize - 0xffff : 0;
    for (;;) {
        if (u32(data + pos) == kEndSig && pos + kEndSize + u16(data + pos + 20) <= len) break;
        if (pos == stop) return false;
        --pos;
    }
    const uint8_t* end = data + pos;
    const uint32_t entries = u16(end + 10);
    const uint32_t cd_size = u32(end + 12);
    const uint32_t cd_off = u32(end + 16);
    if (u16(end + 4) != 0 || u16(end + 6) != 0) return false;   // multi-disk
    if (uint64_t(cd_off) + cd_size > pos) return false;
    cd_offset_ = cd_off;
    cd_end_ = cd_off + cd_size;
    count_ = int(entries);
    return true;
}

bool Reader::next(uint32_t& it, Entry& e) const
{
    if (it + 46 > cd_end_) return false;
    const uint8_t* p = data_ + it;
    if (u32(p) != kCentralSig) return false;
    e.method = u16(p + 10);
    e.crc = u32(p + 16);
    e.comp_size = u32(p + 20);
    e.size = u32(p + 24);
    e.name_len = u16(p + 28);
    const uint32_t extra = u16(p + 30), comment = u16(p + 32);
    e.local_offset = u32(p + 42);
    e.name = reinterpret_cast<const char*>(p + 46);
    const uint64_t next = uint64_t(it) + 46 + e.name_len + extra + comment;
    if (next > cd_end_) return false;
    it = uint32_t(next);
    return true;
}

bool Reader::extract(const Entry& e, uint8_t* out) const
{
    if (uint64_t(e.local_offset) + 30 > len_) return false;
    const uint8_t* lh = data_ + e.local_offset;
    if (u32(lh) != kLocalSig) return false;
    if (u16(lh + 6) & 1) return false;                       // encrypted
    const uint64_t start = uint64_t(e.local_offset) + 30 + u16(lh + 26) + u16(lh + 28);
    if (start + e.comp_size > len_) return false;
    const uint8_t* src = data_ + start;
    if (e.method == 0) {
        if (e.comp_size != e.size) return false;
        std::memcpy(out, src, e.size);
    } else if (e.method == 8) {
        if (inflate::raw(src, e.comp_size, out, e.size) != long(e.size)) return false;
    } else {
        return false;
    }
    return crc32::of(out, e.size) == e.crc;
}

} // namespace zip
