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

bool s_available[16] = {};      // games past 16 would need this raised

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
    for (int g = 0; g < games::kGameCount && g < 16; ++g) {
        s_available[g] = romset::available(*games::kGames[g].set, src, n);
        ESP_LOGI(kTag, "%-10s %s", games::kGames[g].id, s_available[g] ? "ready" : "no ROMs");
    }
}

bool available(int game) { return game >= 0 && game < 16 && s_available[game]; }

int available_count()
{
    int n = 0;
    for (int g = 0; g < games::kGameCount && g < 16; ++g) n += s_available[g];
    return n;
}

void list_ids(char* out, size_t cap)
{
    size_t used = 0;
    if (cap) out[0] = 0;
    for (int g = 0; g < games::kGameCount && g < 16; ++g) {
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
    if (set.region_count > 8) return false;
    for (int r = 0; r < set.region_count; ++r) {
        out.regions[r] = static_cast<uint8_t*>(heap_caps_malloc(set.regions[r].size, MALLOC_CAP_8BIT));
        if (!out.regions[r]) {
            ESP_LOGE(kTag, "%s: no memory for region %s (%u bytes)", game.id, set.regions[r].name,
                     unsigned(set.regions[r].size));
            unload(out);
            return false;
        }
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
        unload(out);
    }
    std::free(status);
    return ok;
}

void unload(Loaded& l)
{
    for (int r = 0; r < l.count; ++r) heap_caps_free(l.regions[r]);
    l = Loaded{};
}

}  // namespace catalog
