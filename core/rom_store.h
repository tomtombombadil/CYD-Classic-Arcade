// The board's ROM store, written for this project (MIT): the user's ROM
// zips, kept as they are (compressed) in the `roms` flash partition, so the
// ROM matcher can read them in place. Plain C++ over a small flash
// interface, so it is tested on the PC (tools/host_tests) with a fake flash.
//
// Layout, in 4 KB sectors:
//   sector 0  directory: "CAR1", entry count, next free offset, then
//             entries {name[32], offset, size, crc, flags}
//   then      the files, one after another (4-byte aligned)
// Files are only appended; storing a name again hides the older copy. The
// space comes back with clear(). The directory is rewritten after each
// stored file.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rom_store {

constexpr uint32_t kSector = 4096;
constexpr int      kNameMax = 31;      // characters, without the NUL
constexpr int      kMaxEntries = 80;

// The flash the store lives in (a partition on the board, a buffer on the PC).
class Flash {
public:
    virtual ~Flash() = default;
    virtual uint32_t size() const = 0;
    virtual bool read(uint32_t offset, void* out, uint32_t len) = 0;
    virtual bool erase_sector(uint32_t offset) = 0;            // offset is sector-aligned
    virtual bool write(uint32_t offset, const void* data, uint32_t len) = 0;
    // The whole area readable in place (memory-mapped on the board).
    virtual const uint8_t* map() = 0;
};

struct Entry {
    char     name[kNameMax + 1];
    uint32_t offset;
    uint32_t size;
    uint32_t crc;
    uint32_t flags;     // bit 0: live
};

class Store {
public:
    // Reads the directory; a blank or damaged one = an empty store.
    void open(Flash& flash);

    int          count() const { return count_; }                 // live and hidden
    const Entry& entry(int i) const { return dir_.entries[i]; }
    bool         live(int i) const { return dir_.entries[i].flags & 1; }
    uint32_t     free_bytes() const;
    // A stored file's bytes, read in place.
    const uint8_t* data(int i);

    // Forgets every file (the space is reused, sectors erased as written).
    bool clear();

    // Someone else wrote into the free sectors after the files (the game
    // loader borrows them for ROMs that don't fit its scratch area): they
    // are erased again before a file reaches them. The sector the files end
    // in is never lent.
    void forget_erased();

    // Storing a file: begin(), add() its bytes in any pieces, finish().
    // finish() checks the size and CRC and only then adds the directory
    // entry; a file that doesn't finish leaves the store as it was.
    bool begin(const char* name, uint32_t size, uint32_t crc);
    bool add(const uint8_t* data, uint32_t len);
    bool finish();
    void abort() { writing_ = false; }

    // A name a file may have: 1..31 of A-Z a-z 0-9 . _ -
    static bool valid_name(const char* name);

private:
    struct Dir {
        char     magic[4];
        uint32_t count;
        uint32_t next_free;
        uint32_t reserved;
        Entry    entries[kMaxEntries];
    };
    static_assert(sizeof(Dir) <= kSector, "the directory must fit one sector");

    bool write_dir();
    bool ensure_erased(uint32_t end);   // erase sectors so [.., end) is writable

    Flash*   flash_ = nullptr;
    Dir      dir_{};
    int      count_ = 0;
    uint32_t erased_to_ = kSector;      // bytes in [next_free, erased_to_) are erased

    // the file being stored
    bool     writing_ = false;
    Entry    pending_{};
    uint32_t written_ = 0;
    uint32_t crc_state_ = 0;
};

} // namespace rom_store
