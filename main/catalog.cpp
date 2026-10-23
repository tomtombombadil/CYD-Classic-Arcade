// Game catalogue over the ROM store (see catalog.h).
#include "catalog.h"

#include "arena.h"
#include "devlog.h"
#include "flash_store.h"
#include "core/scratch_layout.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_log.h>

namespace catalog {
namespace {

constexpr const char* kTag = "catalog";
constexpr int kMaxSources = rom_store::kMaxEntries;

bool s_available[games::kMaxGames] = {};
char s_error[48] = "";

// The live stored zips as ROM matcher sources (store locked by the caller).
int sources(romset::Source* out)
{
    rom_store::Store& st = flash_store::store();
    int n = 0;
    for (int i = 0; i < st.count() && n < kMaxSources; ++i) {
        if (!st.live(i)) continue;
        out[n].name = st.entry(i).name;
        out[n].data = st.data(i);
        out[n].len = st.entry(i).size;
        if (out[n].data) ++n;
    }
    return n;
}

}  // namespace

void refresh()
{
    static romset::Source src[kMaxSources];
    flash_store::Lock lock;
    const int n = sources(src);
    char none[160] = "";
    size_t used = 0;
    int ready = 0;
    for (int g = 0; g < games::kGameCount && g < games::kMaxGames; ++g) {
        s_available[g] = romset::available(*games::kGames[g].set, src, n);
        ESP_LOGI(kTag, "%-10s %s", games::kGames[g].id, s_available[g] ? "ready" : "no ROMs");
        if (s_available[g]) {
            ++ready;
        } else if (used < sizeof none) {
            const int k = std::snprintf(none + used, sizeof none - used, " %s", games::kGames[g].id);
            if (k > 0) used += size_t(k);
        }
    }
    devlog::add("Games ready %d of %d; %d zips stored, %u KB free%s%s", ready, games::kGameCount, n,
                unsigned(flash_store::store().free_bytes() / 1024), used ? "; no ROMs:" : "", none);
}

bool available(int game) { return game >= 0 && game < games::kMaxGames && s_available[game]; }

int available_count()
{
    int n = 0;
    for (int g = 0; g < games::kGameCount && g < games::kMaxGames; ++g) n += s_available[g];
    return n;
}

void list_ids(char* out, size_t cap)
{
    size_t used = 0;
    if (cap) out[0] = 0;
    for (int g = 0; g < games::kGameCount && g < games::kMaxGames; ++g) {
        if (!s_available[g]) continue;
        const int k = std::snprintf(out + used, cap - used, "%s%s", used ? " " : "", games::kGames[g].id);
        if (k < 0 || size_t(k) >= cap - used) break;
        used += size_t(k);
    }
}

namespace {

// The scratch flash: a header sector saying which game's ROMs are there and
// where, then the regions. A game started again finds its ROMs already in
// place (checked byte for byte) and writes nothing.
constexpr uint32_t kMagic = 0x31435343;     // "CSC1"
constexpr uint32_t kData = rom_store::kSector;

struct Header {
    uint32_t magic;
    char     game[16];
    int32_t  variant;
    uint32_t count;
    uint8_t  region[romset::kMaxRegions];
    uint32_t offset[romset::kMaxRegions];
    uint32_t size[romset::kMaxRegions];
    uint32_t complete;
};

struct FlashSink {
    uint32_t base[romset::kMaxRegions] = {};
    bool verify = false;        // compare with what's there instead of writing
    bool mismatch = false;
};

bool flash_put(void* ctx, int region, uint32_t off, const uint8_t* data, uint32_t len)
{
    FlashSink& f = *static_cast<FlashSink*>(ctx);
    const uint32_t at = f.base[region] + off;
    if (f.verify) {
        if (std::memcmp(flash_store::scratch_map(int32_t(at)), data, len) != 0) f.mismatch = true;
        return true;
    }
    return flash_store::scratch_write(int32_t(at), data, len);
}

// Zeros where no loaded ROM lands in a flash region (it was erased to FF;
// RAM regions are cleared the same way by romset::load).
bool zero_gaps(const romset::Set& set, const romset::Status* status, int region, uint32_t base, uint8_t* buf,
               uint32_t buf_size)
{
    struct Span { uint32_t from, to; };
    Span spans[64];
    int n = 0;
    for (int i = 0; i < set.rom_count && n < 64; ++i) {
        const romset::Rom& r = set.roms[i];
        if (r.region != region || status[i] != romset::Status::Ok) continue;
        Span sp{r.offset, r.offset + r.size};
        int j = n++;
        while (j > 0 && spans[j - 1].from > sp.from) {
            spans[j] = spans[j - 1];
            --j;
        }
        spans[j] = sp;
    }
    std::memset(buf, 0, buf_size);
    uint32_t pos = 0;
    const uint32_t end = set.regions[region].size;
    auto fill = [&](uint32_t from, uint32_t to) {
        while (from < to) {
            const uint32_t k = to - from < buf_size ? to - from : buf_size;
            if (!flash_store::scratch_write(int32_t(base + from), buf, k)) return false;
            from += k;
        }
        return true;
    };
    for (int i = 0; i < n; ++i) {
        if (spans[i].from > pos && !fill(pos, spans[i].from)) return false;
        if (spans[i].to > pos) pos = spans[i].to;
    }
    return pos >= end || fill(pos, end);
}

}  // namespace

bool load(const games::Info& game, Loaded& out)
{
    const romset::Set& set = *game.set;
    out = Loaded{};
    s_error[0] = 0;
    if (set.region_count > romset::kMaxRegions) {
        ESP_LOGE(kTag, "%s: %d ROM regions, the loader holds %d", game.id, set.region_count, romset::kMaxRegions);
        std::snprintf(s_error, sizeof s_error, "Too many ROM regions");
        return false;
    }
    out.count = set.region_count;
    for (int r = 0; r < set.region_count; ++r) out.sizes[r] = set.regions[r].size;

    // Where each region goes: PSRAM if the board has it; what's left of the
    // game area (biggest regions to flash first, so the most fit); else
    // flash, read in place.
    bool flash[romset::kMaxRegions] = {};
    for (int r = 0; r < set.region_count; ++r) {
        out.regions[r] = static_cast<uint8_t*>(heap_caps_malloc(out.sizes[r], MALLOC_CAP_SPIRAM));
        out.in_psram[r] = out.regions[r] != nullptr;
    }
    // Each block in the area costs a few bytes of its own
    const uint32_t budget =
        arena::room_total() > 64u * romset::kMaxRegions ? arena::room_total() - 64u * romset::kMaxRegions : 0;
    // Regions going to flash are unpacked a ROM at a time through a buffer
    // the size of their biggest ROM, which must fit in the area too: with
    // the area full of regions it came from the rest of RAM, and Star Wars'
    // 16 KB didn't fit the ~8 KB there (Tom's 3.5", v0.16.0)
    auto biggest_flash_rom = [&]() {
        uint32_t b = 0;
        for (int i = 0; i < set.rom_count; ++i)
            if (flash[set.roms[i].region] && set.roms[i].size > b) b = set.roms[i].size;
        return b;
    };
    for (;;) {
        uint32_t want = 0;
        int biggest = -1;
        for (int r = 0; r < set.region_count; ++r) {
            if (out.regions[r] || flash[r]) continue;
            want += out.sizes[r];
            if (biggest < 0 || out.sizes[r] > out.sizes[biggest]) biggest = r;
        }
        const uint32_t unpack = biggest_flash_rom();
        if (biggest < 0 || want + (unpack ? unpack + 64 : 0) <= budget) break;
        flash[biggest] = true;
    }
    for (int r = 0; r < set.region_count; ++r) {
        if (out.regions[r] || flash[r]) continue;
        out.regions[r] = static_cast<uint8_t*>(arena::alloc(out.sizes[r]));
        if (out.regions[r]) out.ram_used += out.sizes[r];
        else flash[r] = true;
    }

    // The flash regions' places: after the header sector while they fit the
    // area, the rest below it in the store's free room (offsets as uint32,
    // two's complement)
    FlashSink sink_ctx;
    Header want{};
    want.magic = kMagic;
    std::snprintf(want.game, sizeof want.game, "%s", game.id);
    uint32_t biggest_rom = 0;
    uint32_t sizes[romset::kMaxRegions];
    int32_t places[romset::kMaxRegions];
    int n_flash = 0;
    for (int r = 0; r < set.region_count; ++r)
        if (flash[r]) sizes[n_flash++] = out.sizes[r];
    const scratch_layout::Result layout =
        scratch_layout::place(sizes, n_flash, flash_store::scratch_size(), kData, places);
    const uint32_t pos = layout.top;
    for (int r = 0, k = 0; r < set.region_count; ++r) {
        if (!flash[r]) continue;
        sink_ctx.base[r] = uint32_t(places[k++]);
        want.region[want.count] = uint8_t(r);
        want.offset[want.count] = sink_ctx.base[r];
        want.size[want.count] = out.sizes[r];
        want.count++;
        out.in_flash[r] = true;
        out.flash_used += out.sizes[r];
    }
    for (int i = 0; i < set.rom_count; ++i)
        if (flash[set.roms[i].region] && set.roms[i].size > biggest_rom) biggest_rom = set.roms[i].size;
    if (want.count && (!flash_store::scratch_size() || layout.below > flash_store::scratch_below())) {
        ESP_LOGE(kTag, "%s: %u + %u bytes of ROM for flash, %u + %u bytes of room", game.id, unsigned(pos),
                 unsigned(layout.below), unsigned(flash_store::scratch_size()),
                 unsigned(flash_store::scratch_below()));
        std::snprintf(s_error, sizeof s_error, flash_store::scratch_size() ? "Not enough flash room for its ROMs"
                                                                          : "No flash room: reinstall, erasing");
        unload(out);
        return false;
    }

    static romset::Source src[kMaxSources];
    romset::Status* status = static_cast<romset::Status*>(std::calloc(size_t(set.rom_count), sizeof(romset::Status)));
    // The unpacking buffer: from the game area if there's room (given back
    // right after), else for a moment from the rest of RAM
    uint8_t* scratch = biggest_rom ? static_cast<uint8_t*>(arena::alloc(biggest_rom)) : nullptr;
    const bool scratch_in_area = scratch != nullptr;
    if (biggest_rom && !scratch) scratch = static_cast<uint8_t*>(heap_caps_malloc(biggest_rom, MALLOC_CAP_8BIT));
    bool ok = status && (scratch || !biggest_rom);
    if (status && !ok) {
        std::snprintf(s_error, sizeof s_error, "Out of memory: no %u KB to unpack its ROMs",
                      unsigned((biggest_rom + 1023) / 1024));
        ESP_LOGE(kTag, "%s: %s", game.id, s_error);
    }
    if (ok) {
        flash_store::Lock lock;
        const int n = sources(src);
        want.variant = romset::pick_variant(set, src, n);
        for (int r = 0; r < set.region_count; ++r)
            if (flash[r]) out.regions[r] = const_cast<uint8_t*>(flash_store::scratch_map(int32_t(sink_ctx.base[r])));
        uint8_t* bufs[romset::kMaxRegions] = {};
        for (int r = 0; r < set.region_count; ++r) bufs[r] = flash[r] ? nullptr : out.regions[r];
        romset::Sink sink{flash_put, &sink_ctx, scratch, biggest_rom};
        Header have;
        std::memcpy(&have, flash_store::scratch_map(0), sizeof have);
        want.complete = 1;
        const bool same = want.count && std::memcmp(&have, &want, sizeof want) == 0;
        if (same) {     // the same game's ROMs are there: check them
            sink_ctx.verify = true;
            ok = romset::load(set, src, n, bufs, status, &sink);
        }
        if (want.count && (!same || sink_ctx.mismatch || !ok)) {
            ESP_LOGI(kTag, "%s: writing %u KB of ROM to flash", game.id, unsigned(out.flash_used / 1024));
            sink_ctx.verify = false;
            ok = flash_store::scratch_erase(0, pos);
            if (ok && layout.below) ok = flash_store::scratch_erase(-int32_t(layout.below), layout.below);
            if (ok) ok = romset::load(set, src, n, bufs, status, &sink);
            for (int r = 0; ok && r < set.region_count; ++r)
                if (flash[r]) ok = zero_gaps(set, status, r, sink_ctx.base[r], scratch, biggest_rom);
            if (ok) ok = flash_store::scratch_write(0, &want, sizeof want);
        } else if (!want.count) {
            ok = romset::load(set, src, n, bufs, status);
        }
    }
    if (scratch_in_area) arena::free(scratch);
    else heap_caps_free(scratch);
    if (!ok && s_error[0]) {
        unload(out);    // out of memory, said above (the runner logs it)
    } else if (!ok) {
        char msg[512];
        if (status) romset::report(set, status, msg, sizeof msg);
        ESP_LOGE(kTag, "%s: ROMs not loaded\n%s", game.id, status ? msg : "out of memory");
        std::snprintf(s_error, sizeof s_error, "%s", status ? "A ROM is missing or bad" : "Out of memory");
        for (int i = 0; status && i < set.rom_count; ++i) {
            const romset::Rom& rom = set.roms[i];
            if (rom.optional || status[i] == romset::Status::Ok || status[i] == romset::Status::Unused) continue;
            std::snprintf(s_error, sizeof s_error, "%s %s", rom.name,
                          status[i] == romset::Status::Missing ? "missing" : "bad");
            break;
        }
        unload(out);
    } else if (out.flash_used) {
        ESP_LOGI(kTag, "%s: %u KB of ROM read from flash", game.id, unsigned(out.flash_used / 1024));
    }
    std::free(status);
    return ok;
}

void unload(Loaded& l)
{
    // Regions in the game area go with it (arena::reset); PSRAM ones here
    for (int r = 0; r < l.count; ++r)
        if (l.in_psram[r]) heap_caps_free(l.regions[r]);
    l = Loaded{};
}

const char* last_error() { return s_error[0] ? s_error : "Reload them from the web page"; }

}  // namespace catalog
