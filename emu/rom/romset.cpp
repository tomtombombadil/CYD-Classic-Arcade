// ROM set matcher - see romset.h.
#include "romset.h"

#include "crc32.h"
#include "zip.h"

#include <cstdio>
#include <cstring>

namespace romset {

namespace {

// Case-insensitive compare of the last path component of `path` with `name`.
bool same_file_name(const char* path, size_t len, const char* name)
{
    size_t start = len;
    while (start > 0 && path[start - 1] != '/' && path[start - 1] != '\\') --start;
    const size_t n = std::strlen(name);
    if (len - start != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = path[start + i], b = name[i];
        if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

// A name match only tells us the file is wrong when nothing better was found.
void note_name_match(Status& st, uint32_t found_size, uint32_t want_size)
{
    if (st == Status::Ok || st == Status::Bad) return;
    st = found_size == want_size ? Status::WrongCrc : Status::WrongSize;
}

} // namespace

bool load(const Set& set, const Source* sources, int source_count, uint8_t* const* regions,
          Status* status)
{
    for (int r = 0; r < set.region_count; ++r) std::memset(regions[r], 0, set.regions[r].size);
    for (int i = 0; i < set.rom_count; ++i) status[i] = Status::Missing;

    for (int s = 0; s < source_count; ++s) {
        const Source& src = sources[s];
        zip::Reader z;
        if (zip::looks_like_zip(src.data, src.len) && z.open(src.data, src.len)) {
            uint32_t it = z.begin();
            zip::Entry e;
            while (z.next(it, e)) {
                for (int i = 0; i < set.rom_count; ++i) {
                    const Rom& rom = set.roms[i];
                    if (status[i] == Status::Ok) continue;
                    if (e.size == rom.size && e.crc == rom.crc) {
                        if (uint64_t(rom.offset) + rom.size > set.regions[rom.region].size) continue;
                        uint8_t* dst = regions[rom.region] + rom.offset;
                        status[i] = z.extract(e, dst) ? Status::Ok : Status::Bad;
                    } else if (same_file_name(e.name, e.name_len, rom.name)) {
                        note_name_match(status[i], e.size, rom.size);
                    }
                }
            }
            continue;
        }
        // A loose file: match by size and CRC of its whole contents.
        const uint32_t crc = crc32::of(src.data, src.len);
        for (int i = 0; i < set.rom_count; ++i) {
            const Rom& rom = set.roms[i];
            if (status[i] == Status::Ok) continue;
            if (src.len == rom.size && crc == rom.crc) {
                if (uint64_t(rom.offset) + rom.size > set.regions[rom.region].size) continue;
                std::memcpy(regions[rom.region] + rom.offset, src.data, rom.size);
                status[i] = Status::Ok;
            } else if (src.name && same_file_name(src.name, std::strlen(src.name), rom.name)) {
                note_name_match(status[i], uint32_t(src.len), rom.size);
            }
        }
    }

    for (int i = 0; i < set.rom_count; ++i)
        if (status[i] != Status::Ok && !set.roms[i].optional) return false;
    return true;
}

bool available(const Set& set, const Source* sources, int source_count)
{
    for (int i = 0; i < set.rom_count; ++i) {
        const Rom& rom = set.roms[i];
        if (rom.optional) continue;
        bool found = false;
        for (int s = 0; s < source_count && !found; ++s) {
            const Source& src = sources[s];
            zip::Reader z;
            if (zip::looks_like_zip(src.data, src.len) && z.open(src.data, src.len)) {
                uint32_t it = z.begin();
                zip::Entry e;
                while (!found && z.next(it, e)) found = e.size == rom.size && e.crc == rom.crc;
            } else {
                found = src.len == rom.size && crc32::of(src.data, src.len) == rom.crc;
            }
        }
        if (!found) return false;
    }
    return true;
}

const char* status_name(Status s)
{
    switch (s) {
    case Status::Missing:   return "missing";
    case Status::Ok:        return "ok";
    case Status::WrongCrc:  return "wrong crc";
    case Status::WrongSize: return "wrong size";
    default:                return "unreadable";
    }
}

int report(const Set& set, const Status* status, char* out, size_t cap)
{
    int bad = 0;
    size_t used = 0;
    if (cap) out[0] = 0;
    for (int i = 0; i < set.rom_count; ++i) {
        if (status[i] == Status::Ok) continue;
        const Rom& rom = set.roms[i];
        if (!rom.optional) ++bad;
        if (used + 1 >= cap) continue;
        const int n = std::snprintf(out + used, cap - used, "%s ROM %s crc %08x (%u bytes)%s\n",
                                    status_name(status[i]), rom.name, unsigned(rom.crc),
                                    unsigned(rom.size), rom.optional ? " - optional" : "");
        if (n > 0) used += size_t(n) < cap - used ? size_t(n) : cap - used - 1;
    }
    return bad;
}

} // namespace romset
