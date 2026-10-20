// A board on the PC for the web page's ROM loader (MIT): core/usb_import +
// core/rom_store on an in-memory flash the size of the `roms` partition,
// over stdin / stdout (binary-safe), with log lines in between like the
// firmware prints. DONE answers with the games the stored zips make
// complete (emu/games.h + romset::available), as the firmware does.
// tools/host_tests/loader_test.mjs drives it with web/roms/loader.js.
//
//   fake_board [--flash-kb N] [--boot-ms N] [--mute] [--log-ms N]
//     --flash-kb  the store's size (default 1728 = the roms partition)
//     --boot-ms   input in the first N ms is lost (the board resetting
//                 when the port opens)
//     --mute      never answers (not our firmware)
//     --log-ms    a log line every N ms (default 300)
#include "../../core/rom_store.h"
#include "../../core/usb_import.h"
#include "../../emu/games.h"
#include "../../emu/rom/romset.h"

#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

class MemFlash : public rom_store::Flash {
public:
    explicit MemFlash(uint32_t size) : mem(size, 0xFF) {}
    uint32_t size() const override { return uint32_t(mem.size()); }
    bool read(uint32_t off, void* out, uint32_t len) override
    {
        if (off + uint64_t(len) > mem.size()) return false;
        std::memcpy(out, mem.data() + off, len);
        return true;
    }
    bool erase_sector(uint32_t off) override
    {
        if (off % rom_store::kSector || off + rom_store::kSector > mem.size()) return false;
        std::memset(mem.data() + off, 0xFF, rom_store::kSector);
        return true;
    }
    bool write(uint32_t off, const void* data, uint32_t len) override
    {
        if (off + uint64_t(len) > mem.size()) return false;
        const uint8_t* d = static_cast<const uint8_t*>(data);
        for (uint32_t i = 0; i < len; ++i) mem[off + i] &= d[i];      // NOR: bits only clear
        return true;
    }
    const uint8_t* map() override { return mem.data(); }
    std::vector<uint8_t> mem;
};

uint32_t now_ms()
{
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return uint32_t(duration_cast<milliseconds>(steady_clock::now() - t0).count());
}

void out_line(const char* line)
{
    std::fputs(line, stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void send(void*, const char* line) { out_line(line); }

rom_store::Store g_store;

void game_list(void*, char* out, size_t cap)
{
    std::vector<romset::Source> src;
    for (int i = 0; i < g_store.count(); ++i)
        if (g_store.live(i)) src.push_back({g_store.entry(i).name, g_store.data(i), g_store.entry(i).size});
    size_t used = 0;
    if (cap) out[0] = 0;
    for (int g = 0; g < games::kGameCount; ++g) {
        const games::Info& gi = games::kGames[g];
        if (!romset::available(*gi.set, src.data(), int(src.size()))) continue;
        const int n = std::snprintf(out + used, cap - used, "%s%s", used ? " " : "", gi.id);
        if (n < 0 || used + size_t(n) >= cap) break;
        used += size_t(n);
    }
}

// The board's log for LOG: three made-up lines (one empty)
bool log_line(void*, int i, char* out, size_t cap)
{
    static const char* const kLines[] = {"CYD Classic Arcade log", "", "[1 0.5s] Start v0.2.0-fake on a fake board"};
    if (i < 0 || i >= int(sizeof kLines / sizeof kLines[0])) return false;
    std::snprintf(out, cap, "%s", kLines[i]);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    uint32_t flash_kb = 0x1B0000 / 1024, boot_ms = 0, log_ms = 300;
    bool mute = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--flash-kb" && i + 1 < argc) flash_kb = uint32_t(std::atoi(argv[++i]));
        else if (a == "--boot-ms" && i + 1 < argc) boot_ms = uint32_t(std::atoi(argv[++i]));
        else if (a == "--log-ms" && i + 1 < argc) log_ms = uint32_t(std::atoi(argv[++i]));
        else if (a == "--mute") mute = true;
        else { std::fprintf(stderr, "usage: fake_board [--flash-kb N] [--boot-ms N] [--mute] [--log-ms N]\n"); return 2; }
    }

    MemFlash flash(flash_kb * 1024);
    g_store.open(flash);
    usb_import::Session session;
    session.init(g_store, &send, &game_list, nullptr, "v0.2.0-fake");
    session.set_log(&log_line);

    out_line("ets Jun  8 2016 00:22:57");
    out_line("rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)");
    uint32_t next_log = now_ms() + log_ms, logs = 0;
    bool booted = false;
    std::vector<uint8_t> buf(4096);
    for (;;) {
        const uint32_t t = now_ms();
        if (!booted && t >= boot_ms) {
            booted = true;
            out_line("I (812) arcade: CYD Classic Arcade v0.2.0-fake, fake board");
        }
        if (t >= next_log) {
            char line[80];
            std::snprintf(line, sizeof line, "I (%u) arcade: heap 142000 free, tick %u", unsigned(t), ++logs);
            out_line(line);
            next_log = t + log_ms;
        }
        pollfd p{0, POLLIN, 0};
        const int r = poll(&p, 1, 20);
        if (r > 0) {
            const ssize_t n = read(0, buf.data(), buf.size());
            if (n <= 0) break;                                  // the page closed the port
            if (booted && !mute) session.feed(buf.data(), size_t(n), now_ms());
        }
        session.tick(now_ms());
        if (session.take_done()) out_line("I (0) arcade: rescanning the ROM store");
    }
    return 0;
}
