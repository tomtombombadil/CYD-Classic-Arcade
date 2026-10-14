// The `roms` partition (partitions.csv: data, subtype 0x40) for the ROM store.
#include "flash_store.h"

#include <esp_log.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace flash_store {
namespace {

constexpr const char* kTag = "roms";

class PartitionFlash : public rom_store::Flash {
public:
    const esp_partition_t* part = nullptr;
    const uint8_t* mapped = nullptr;
    esp_partition_mmap_handle_t handle = 0;

    uint32_t size() const override { return part ? part->size : 0; }
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
    s_store.open(s_flash);
    int live = 0;
    for (int i = 0; i < s_store.count(); ++i) live += s_store.live(i);
    ESP_LOGI(kTag, "%d zips stored, %u KB free", live, unsigned(s_store.free_bytes() / 1024));
    return true;
}

rom_store::Flash& flash() { return s_flash; }
rom_store::Store& store() { return s_store; }
void lock() { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
void unlock() { xSemaphoreGiveRecursive(s_lock); }

}  // namespace flash_store
