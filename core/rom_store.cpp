// The board's ROM store - see rom_store.h.
#include "rom_store.h"

#include "../emu/rom/crc32.h"

#include <cstring>

namespace rom_store {

namespace {

constexpr char kMagic[4] = {'C', 'A', 'R', '1'};

uint32_t align4(uint32_t v) { return (v + 3) & ~3u; }
uint32_t round_up_sector(uint32_t v) { return (v + kSector - 1) / kSector * kSector; }

} // namespace

void Store::open(Flash& flash)
{
    flash_ = &flash;
    writing_ = false;
    std::memset(&dir_, 0, sizeof dir_);
    count_ = 0;
    Dir d;
    const bool ok = flash.read(0, &d, sizeof d) && std::memcmp(d.magic, kMagic, 4) == 0 &&
                    d.count <= uint32_t(kMaxEntries) && d.next_free >= kSector &&
                    d.next_free <= flash.size();
    if (ok) {
        dir_ = d;
        count_ = int(d.count);
        // Drop entries that don't fit (a damaged directory) rather than trust them.
        for (int i = 0; i < count_; ++i) {
            Entry& e = dir_.entries[i];
            e.name[kNameMax] = 0;
            if (e.offset < kSector || e.offset + uint64_t(e.size) > d.next_free) e.flags = 0;
        }
    } else {
        std::memcpy(dir_.magic, kMagic, 4);
        dir_.next_free = kSector;
    }
    // Later sectors are erased as files reach them; the rest of next_free's
    // own sector is checked when the next file begins (a file cut off by a
    // power loss may have left bytes there).
    erased_to_ = round_up_sector(dir_.next_free);
}

uint32_t Store::free_bytes() const
{
    if (!flash_) return 0;
    const uint32_t end = flash_->size();
    return end > dir_.next_free ? end - dir_.next_free : 0;
}

const uint8_t* Store::data(int i)
{
    const uint8_t* base = flash_ ? flash_->map() : nullptr;
    return base ? base + dir_.entries[i].offset : nullptr;
}

bool Store::clear()
{
    if (!flash_) return false;
    writing_ = false;
    std::memset(&dir_, 0, sizeof dir_);
    std::memcpy(dir_.magic, kMagic, 4);
    dir_.next_free = kSector;
    count_ = 0;
    erased_to_ = kSector;
    return write_dir();
}

bool Store::valid_name(const char* name)
{
    const size_t n = std::strlen(name);
    if (n == 0 || n > size_t(kNameMax)) return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = name[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

bool Store::ensure_erased(uint32_t end)
{
    while (erased_to_ < end) {
        if (!flash_->erase_sector(erased_to_)) return false;
        erased_to_ += kSector;
    }
    return true;
}

bool Store::begin(const char* name, uint32_t size, uint32_t crc)
{
    writing_ = false;
    if (!flash_ || !valid_name(name)) return false;
    // A slot: a new one, or reuse a hidden entry's slot when the list is full.
    int live_same = 0;
    for (int i = 0; i < count_; ++i)
        if (live(i) && std::strcmp(entry(i).name, name) == 0) ++live_same;
    if (count_ >= kMaxEntries) {
        bool hidden = false;
        for (int i = 0; i < count_ && !hidden; ++i) hidden = !live(i);
        if (!hidden && !live_same) return false;
    }
    // The rest of the last sector must still be erased; if a file was cut
    // off there, start at the next sector.
    uint32_t start = dir_.next_free;
    uint8_t buf[256];
    for (uint32_t at = start; at < erased_to_;) {
        const uint32_t n = erased_to_ - at < sizeof buf ? erased_to_ - at : uint32_t(sizeof buf);
        if (!flash_->read(at, buf, n)) return false;
        bool blank = true;
        for (uint32_t k = 0; k < n && blank; ++k) blank = buf[k] == 0xFF;
        if (!blank) { start = erased_to_; break; }
        at += n;
    }
    if (start + uint64_t(size) > flash_->size()) return false;
    std::memset(&pending_, 0, sizeof pending_);
    std::strncpy(pending_.name, name, kNameMax);
    pending_.offset = start;
    pending_.size = size;
    pending_.crc = crc;
    pending_.flags = 1;
    written_ = 0;
    crc_state_ = 0;
    writing_ = true;
    return true;
}

bool Store::add(const uint8_t* data, uint32_t len)
{
    if (!writing_) return false;
    if (written_ + uint64_t(len) > pending_.size) { writing_ = false; return false; }
    const uint32_t at = pending_.offset + written_;
    if (!ensure_erased(at + len) || !flash_->write(at, data, len)) { writing_ = false; return false; }
    crc_state_ = crc32::update(crc_state_, data, len);
    written_ += len;
    return true;
}

bool Store::finish()
{
    if (!writing_) return false;
    writing_ = false;
    if (written_ != pending_.size || crc_state_ != pending_.crc) return false;
    // Read back what the flash holds now, in case a write didn't stick.
    uint8_t buf[256];
    uint32_t check = 0;
    for (uint32_t done = 0; done < pending_.size;) {
        const uint32_t n = pending_.size - done < sizeof buf ? pending_.size - done : uint32_t(sizeof buf);
        if (!flash_->read(pending_.offset + done, buf, n)) return false;
        check = crc32::update(check, buf, n);
        done += n;
    }
    if (check != pending_.crc) return false;

    // Hide the older copies, then take a new slot (or a hidden one's).
    for (int i = 0; i < count_; ++i)
        if (live(i) && std::strcmp(entry(i).name, pending_.name) == 0) dir_.entries[i].flags = 0;
    int slot = count_;
    if (count_ >= kMaxEntries) {
        slot = -1;
        for (int i = 0; i < count_ && slot < 0; ++i) if (!live(i)) slot = i;
        if (slot < 0) return false;
    } else {
        ++count_;
    }
    dir_.entries[slot] = pending_;
    dir_.count = uint32_t(count_);
    dir_.next_free = align4(pending_.offset + pending_.size);
    if (dir_.next_free > flash_->size()) dir_.next_free = flash_->size();
    return write_dir();
}

bool Store::write_dir()
{
    if (!flash_->erase_sector(0)) return false;
    return flash_->write(0, &dir_, sizeof dir_);
}

} // namespace rom_store
