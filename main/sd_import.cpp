// SD card ROM import (see sd_import.h).
#include "sd_import.h"

#include "board_select.h"
#include "devlog.h"
#include "flash_store.h"

#include "emu/games.h"
#include "emu/rom/crc32.h"
#include "emu/rom/zip.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_log.h>

#if BOARD_SD_USABLE
#if BOARD_SD_SDMMC
#include <driver/sdmmc_host.h>
#else
#include <driver/sdspi_host.h>
#endif
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#endif

namespace sd_import {

#if !BOARD_SD_USABLE

int run(Status) { return 0; }

#else

namespace {

constexpr const char* kTag = "sd";
constexpr const char* kMount = "/sdcard";
constexpr const char* kFolder = "/sdcard/CYD-Classic-Arcade";
constexpr long kMaxZip = 2 * 1024 * 1024;

#if BOARD_SD_SDMMC
// 4-bit SDMMC on the board's own pins (ESP32-S3: any pins through the GPIO
// matrix)
bool mount(sdmmc_card_t** card)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = gpio_num_t(BOARD_PIN_SD_CLK);
    slot.cmd = gpio_num_t(BOARD_PIN_SD_CMD);
    slot.d0 = gpio_num_t(BOARD_PIN_SD_D0);
    slot.d1 = gpio_num_t(BOARD_PIN_SD_D1);
    slot.d2 = gpio_num_t(BOARD_PIN_SD_D2);
    slot.d3 = gpio_num_t(BOARD_PIN_SD_D3);
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_mount_config_t cfg = {};
    cfg.format_if_mount_failed = false;
    cfg.max_files = 2;
    cfg.allocation_unit_size = 16 * 1024;
    const esp_err_t err = esp_vfs_fat_sdmmc_mount(kMount, &host, &slot, &cfg, card);
    if (err != ESP_OK) {
        ESP_LOGI(kTag, "no SD card (%s)", esp_err_to_name(err));
        return false;
    }
    return true;
}

void unmount(sdmmc_card_t* card) { esp_vfs_fat_sdcard_unmount(kMount, card); }

#else
bool mount(sdmmc_card_t** card)
{
    spi_bus_config_t bus = {};
    bus.mosi_io_num = BOARD_PIN_SD_MOSI;
    bus.miso_io_num = BOARD_PIN_SD_MISO;
    bus.sclk_io_num = BOARD_PIN_SD_SCK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4096;
    if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.gpio_cs = gpio_num_t(BOARD_PIN_SD_CS);
    dev.host_id = SPI3_HOST;
    esp_vfs_fat_mount_config_t cfg = {};
    cfg.format_if_mount_failed = false;
    cfg.max_files = 2;
    cfg.allocation_unit_size = 16 * 1024;
    const esp_err_t err = esp_vfs_fat_sdspi_mount(kMount, &host, &dev, &cfg, card);
    if (err != ESP_OK) {
        ESP_LOGI(kTag, "no SD card (%s)", esp_err_to_name(err));
        spi_bus_free(SPI3_HOST);
        return false;
    }
    return true;
}

void unmount(sdmmc_card_t* card)
{
    esp_vfs_fat_sdcard_unmount(kMount, card);
    spi_bus_free(SPI3_HOST);
}

#endif  // BOARD_SD_SDMMC

// True when the zip's directory lists a ROM of a known game. Reads only the
// end record and the central directory.
bool holds_known_rom(FILE* f, long size)
{
    constexpr long kEnd = 22;
    if (size < kEnd) return false;
    uint8_t end[kEnd];
    if (std::fseek(f, size - kEnd, SEEK_SET) != 0 || std::fread(end, 1, kEnd, f) != size_t(kEnd)) return false;
    auto u16 = [](const uint8_t* p) { return uint32_t(p[0] | p[1] << 8); };
    auto u32 = [](const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); };
    // MAME zips have no archive comment, so the end record is the last 22 bytes.
    if (u32(end) != 0x06054b50) return false;
    const uint32_t cd_size = u32(end + 12), cd_off = u32(end + 16);
    if (cd_size > 64 * 1024 || uint64_t(cd_off) + cd_size > uint64_t(size)) return false;
    // The directory followed by the end record, its offset moved to 0, reads as a zip.
    uint8_t* buf = static_cast<uint8_t*>(std::malloc(cd_size + kEnd));
    if (!buf) return false;
    bool known = false;
    if (std::fseek(f, long(cd_off), SEEK_SET) == 0 && std::fread(buf, 1, cd_size, f) == cd_size) {
        std::memcpy(buf + cd_size, end, kEnd);
        std::memset(buf + cd_size + 16, 0, 4);
        zip::Reader z;
        if (z.open(buf, cd_size + kEnd)) {
            uint32_t it = z.begin();
            zip::Entry e;
            while (!known && z.next(it, e))
                for (int g = 0; g < games::kGameCount && !known; ++g) {
                    const romset::Set& set = *games::kGames[g].set;
                    for (int r = 0; r < set.rom_count && !known; ++r)
                        known = set.roms[r].size == e.size && set.roms[r].crc == e.crc;
                }
        }
    }
    std::free(buf);
    return known;
}

constexpr size_t kBuf = 4096;   // copy buffer, on the heap only while importing

bool already_stored(const char* name, long size)
{
    rom_store::Store& st = flash_store::store();
    for (int i = 0; i < st.count(); ++i)
        if (st.live(i) && std::strcmp(st.entry(i).name, name) == 0 && long(st.entry(i).size) == size) return true;
    return false;
}

bool copy(FILE* f, const char* name, long size, uint8_t* buf)
{
    uint32_t crc = 0;
    std::fseek(f, 0, SEEK_SET);
    for (long done = 0; done < size;) {
        const size_t n = std::fread(buf, 1, kBuf, f);
        if (n == 0) return false;
        crc = crc32::update(crc, buf, n);
        done += long(n);
    }
    rom_store::Store& st = flash_store::store();
    if (!st.begin(name, uint32_t(size), crc)) return false;
    std::fseek(f, 0, SEEK_SET);
    for (long done = 0; done < size;) {
        const size_t n = std::fread(buf, 1, kBuf, f);
        if (n == 0 || !st.add(buf, uint32_t(n))) { st.abort(); return false; }
        done += long(n);
    }
    return st.finish();
}

}  // namespace

int run(Status status)
{
    sdmmc_card_t* card = nullptr;
    if (!mount(&card)) return 0;
    int copied = 0;
    if (DIR* d = opendir(kFolder)) {
        while (dirent* e = readdir(d)) {
            const char* name = e->d_name;
            const size_t len = std::strlen(name);
            if (len < 5 || strcasecmp(name + len - 4, ".zip") != 0) continue;
            if (!rom_store::Store::valid_name(name)) {
                ESP_LOGW(kTag, "skipped %s: name too long or odd characters", name);
                continue;
            }
            char path[300];
            std::snprintf(path, sizeof path, "%s/%s", kFolder, name);
            struct stat stt;
            if (stat(path, &stt) != 0 || stt.st_size <= 0 || stt.st_size > kMaxZip) continue;
            flash_store::Lock lock;
            if (already_stored(name, long(stt.st_size))) continue;
            FILE* f = std::fopen(path, "rb");
            if (!f) continue;
            if (holds_known_rom(f, long(stt.st_size))) {
                char line[64];
                std::snprintf(line, sizeof line, "Copying %s", name);
                if (status) status(line);
                uint8_t* buf = static_cast<uint8_t*>(std::malloc(kBuf));
                const bool ok = buf && copy(f, name, long(stt.st_size), buf);
                std::free(buf);
                if (ok) {
                    ++copied;
                    devlog::add("SD: copied %s (%ld KB)", name, long(stt.st_size) / 1024);
                } else {
                    devlog::add("SD: couldn't copy %s (%ld KB): store full? %u KB free", name,
                                long(stt.st_size) / 1024, unsigned(flash_store::store().free_bytes() / 1024));
                }
            } else {
                devlog::add("SD: %s holds no ROM of a game here: skipped", name);
            }
            std::fclose(f);
        }
        closedir(d);
    } else {
        devlog::add("SD: no %s folder on the card", kFolder + 8);
    }
    unmount(card);
    return copied;
}

#endif

}  // namespace sd_import
