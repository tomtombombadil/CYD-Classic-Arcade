// The `roms` partition (partitions.csv: data, subtype 0x40) for the ROM store.
#include "flash_store.h"

#include <cstring>
#include <esp_log.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace flash_store {
namespace {

constexpr const char* kTag = "roms";
constexpr uint32_t kScratch = 128 * 1024;   // a header sector + the biggest game's ROMs (Star Wars 72 KB)

class PartitionFlash : public rom_store::Flash {
public:
    const esp_partition_t* part = nullptr;
    const uint8_t* mapped = nullptr;
    esp_partition_mmap_handle_t handle = 0;
    uint32_t limit = 0;     // the store's share: the partition less the scratch area

    uint32_t size() const override { return limit; }
    bool read(uint32_t off, void* out, uint32_t len) override
    {
        return esp_partition_read(part, off, out, len) == ESP_OK;
    }
    bool erase_sector(uint32_t off) override
    {
        return esp_partition_erase_range(part, off, rom_store::kSector) == ESP_OK;
    }
    bool write(uint32_t off, const void* data, uint32_t len) override
    {
        return esp_partition_write(part, off, data, len) == ESP_OK;
    }
    // The cache maps flash as it is now, writes included (ESP-IDF keeps
    // mapped regions coherent after esp_partition_write / erase).
    const uint8_t* map() override { return mapped; }
};

PartitionFlash s_flash;
rom_store::Store s_store;
SemaphoreHandle_t s_lock = nullptr;

}  // namespace

bool open()
{
    s_lock = xSemaphoreCreateRecursiveMutex();
    s_flash.part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "roms");
    if (!s_flash.part) {
        ESP_LOGE(kTag, "no roms partition");
        return false;
    }
    const void* p = nullptr;
    const esp_err_t err = esp_partition_mmap(s_flash.part, 0, s_flash.part->size, ESP_PARTITION_MMAP_DATA, &p,
                                             &s_flash.handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "mmap failed: %s", esp_err_to_name(err));
        return false;
    }
    s_flash.mapped = static_cast<const uint8_t*>(p);
    // The scratch area only if the store (written by any version) ends
    // before it: its directory sector says where the files end
    struct { char magic[4]; uint32_t count, next_free; } head{};
    s_flash.limit = s_flash.part->size - kScratch;
    if (esp_partition_read(s_flash.part, 0, &head, sizeof head) == ESP_OK &&
        std::memcmp(head.magic, "CAR1", 4) == 0 && head.next_free > s_flash.limit)
        s_flash.limit = s_flash.part->size;
    s_store.open(s_flash);
    int live = 0;
    for (int i = 0; i < s_store.count(); ++i) live += s_store.live(i);
    ESP_LOGI(kTag, "%d zips stored, %u KB free", live, unsigned(s_store.free_bytes() / 1024));
    return true;
}

uint32_t scratch_size() { return s_flash.part ? s_flash.part->size - s_flash.limit : 0; }

const uint8_t* scratch_map(uint32_t offset) { return s_flash.mapped + s_flash.limit + offset; }

bool scratch_erase(uint32_t offset, uint32_t len)
{
    const uint32_t span = (len + rom_store::kSector - 1) / rom_store::kSector * rom_store::kSector;
    if (offset % rom_store::kSector || offset + span > scratch_size()) return false;
    return esp_partition_erase_range(s_flash.part, s_flash.limit + offset, span) == ESP_OK;
}

bool scratch_write(uint32_t offset, const void* data, uint32_t len)
{
    if (offset + len > scratch_size()) return false;
    return esp_partition_write(s_flash.part, s_flash.limit + offset, data, len) == ESP_OK;
}

rom_store::Flash& flash() { return s_flash; }
rom_store::Store& store() { return s_store; }
void lock() { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
void unlock() { xSemaphoreGiveRecursive(s_lock); }

}  // namespace flash_store
