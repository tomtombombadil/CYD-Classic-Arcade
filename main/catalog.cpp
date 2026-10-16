// Game catalogue over the ROM store (see catalog.h).
#include "catalog.h"

#include "flash_store.h"

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
    for (int g = 0; g < games::kGameCount && g < games::kMaxGames; ++g) {
        s_available[g] = romset::available(*games::kGames[g].set, src, n);
        ESP_LOGI(kTag, "%-10s %s", games::kGames[g].id, s_available[g] ? "ready" : "no ROMs");
    }
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

bool load(const games::Info& game, Loaded& out)
{
    const romset::Set& set = *game.set;
    out = Loaded{};
    if (set.region_count > romset::kMaxRegions) {
        ESP_LOGE(kTag, "%s: %d ROM regions, the loader holds %d", game.id, set.region_count, romset::kMaxRegions);
        return false;
    }
    s_error[0] = 0;
    for (int r = 0; r < set.region_count; ++r) {
        // PSRAM first where the board has it (ESP32-S3 boards): internal
        // RAM stays for the machine and the screen strips
        out.regions[r] = static_cast<uint8_t*>(heap_caps_malloc(set.regions[r].size, MALLOC_CAP_SPIRAM));
        if (!out.regions[r])
            out.regions[r] = static_cast<uint8_t*>(heap_caps_malloc(set.regions[r].size, MALLOC_CAP_8BIT));
        if (!out.regions[r]) {
            const unsigned largest = unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            ESP_LOGE(kTag, "%s: no memory for region %s (%u bytes, largest block %u)", game.id,
                     set.regions[r].name, unsigned(set.regions[r].size), largest);
            std::snprintf(s_error, sizeof s_error, "No RAM for %u KB of ROM (%u KB free)",
                          unsigned(set.regions[r].size / 1024), largest / 1024);
            unload(out);
            return false;
        }
        out.sizes[r] = set.regions[r].size;
        out.count = r + 1;
    }
    static romset::Source src[kMaxSources];
    romset::Status* status = static_cast<romset::Status*>(std::calloc(size_t(set.rom_count), sizeof(romset::Status)));
    bool ok = status != nullptr;
    if (ok) {
        flash_store::Lock lock;
        const int n = sources(src);
        ok = romset::load(set, src, n, out.regions, status);
    }
    if (!ok) {
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
    }
    std::free(status);
    return ok;
}

void unload(Loaded& l)
{
    for (int r = 0; r < l.count; ++r)
        if (!l.in_flash[r]) heap_caps_free(l.regions[r]);
    l = Loaded{};
}

bool move_to_flash(Loaded& l)
{
    int best = -1;
    for (int r = 0; r < l.count; ++r)
        if (!l.in_flash[r] && (best < 0 || l.sizes[r] > l.sizes[best])) best = r;
    if (best < 0) return false;
    flash_store::Lock lock;
    const uint8_t* p = flash_store::scratch_put(l.flash_used, l.regions[best], l.sizes[best]);
    if (!p) {
        ESP_LOGW(kTag, "no scratch flash for a %u byte region", unsigned(l.sizes[best]));
        return false;
    }
    heap_caps_free(l.regions[best]);
    l.regions[best] = const_cast<uint8_t*>(p);     // read only from here on
    l.in_flash[best] = true;
    l.flash_used += (l.sizes[best] + rom_store::kSector - 1) / rom_store::kSector * rom_store::kSector;
    ESP_LOGI(kTag, "a %u byte region moved to flash (RAM short)", unsigned(l.sizes[best]));
    return true;
}

const char* last_error() { return s_error[0] ? s_error : "Reload them from the web page"; }

}  // namespace catalog
