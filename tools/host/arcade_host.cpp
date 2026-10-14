// Headless runner for the emulated machines (host only, MIT).
//
//   arcade_host <game> <zip | rom file | dir>... [options]
//     --frames N          frames to run (default 600)
//     --png-every K       write every K-th frame as PNG (0 = none; default 0)
//     --out DIR           folder for the PNGs (default ".")
//     --wav FILE          write the whole run's audio
//     --rate HZ           audio rate (default 24000)
//     --dips HEX          DIP switch bank (default: the machine's default)
//     --input SCRIPT      e.g. "120+6:coin,200+6:start1,400:left,460:"
//                         F:btns   from frame F hold exactly btns ('+'-joined;
//                                  empty or "none" = release all)
//                         F+N:btns press btns at frame F for N frames
//                         buttons: up down left right coin coin2 start1
//                         start2 service test
//     --verbose           one timing line per frame
//
// Prints per-second timing (emulation and line rendering, ms per frame) and
// a summary, so the cost on the device can be estimated.
#include "../../emu/machines/pacman.h"
#include "../../emu/rom/romset.h"
#include "writers.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct File {
    std::string name;
    std::vector<uint8_t> data;
};

bool read_file(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    const bool ok = n >= 0 && std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

bool is_dir(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string lower(std::string s)
{
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

// A folder: the game's and its parent's ZIPs, plus every small loose file
// (ROM images). Other ZIPs only when the folder is small.
void add_dir(const std::string& dir, const romset::Set& set, std::vector<File>& files)
{
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> names;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        names.push_back(e->d_name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    const std::string game_zip = std::string(set.game) + ".zip";
    const std::string parent_zip = set.parent ? std::string(set.parent) + ".zip" : "";
    for (const std::string& n : names) {
        const std::string path = dir + "/" + n;
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        const std::string ln = lower(n);
        const bool zip = ln.size() > 4 && ln.compare(ln.size() - 4, 4, ".zip") == 0;
        bool take;
        if (zip) take = ln == game_zip || ln == parent_zip || (names.size() < 64 && st.st_size < (4 << 20));
        else     take = st.st_size <= (1 << 20);
        if (!take) continue;
        File f;
        f.name = path;
        if (read_file(path, f.data)) files.push_back(std::move(f));
    }
}

struct Event {
    int      frame;
    int      length;     // 0 = set the held state; > 0 = press for this many frames
    uint16_t buttons;
};

bool parse_buttons(const std::string& s, uint16_t& out)
{
    static const struct { const char* name; uint16_t bit; } kNames[] = {
        {"up", pacman::kUp}, {"down", pacman::kDown}, {"left", pacman::kLeft},
        {"right", pacman::kRight}, {"coin", pacman::kCoin}, {"coin2", pacman::kCoin2},
        {"start1", pacman::kStart1}, {"start2", pacman::kStart2},
        {"service", pacman::kService}, {"test", pacman::kTest}, {"none", 0},
    };
    out = 0;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find_first_of("+|", pos);
        if (end == std::string::npos) end = s.size();
        const std::string name = lower(s.substr(pos, end - pos));
        bool found = false;
        for (const auto& k : kNames)
            if (name == k.name) { out |= k.bit; found = true; }
        if (!found && !name.empty()) {
            std::fprintf(stderr, "unknown button '%s'\n", name.c_str());
            return false;
        }
        pos = end + 1;
    }
    return true;
}

bool parse_script(const std::string& script, std::vector<Event>& events)
{
    size_t pos = 0;
    while (pos < script.size()) {
        size_t end = script.find(',', pos);
        if (end == std::string::npos) end = script.size();
        const std::string item = script.substr(pos, end - pos);
        pos = end + 1;
        if (item.empty()) continue;
        const size_t colon = item.find(':');
        if (colon == std::string::npos) {
            std::fprintf(stderr, "bad input item '%s' (want frame:buttons)\n", item.c_str());
            return false;
        }
        Event e{};
        const std::string when = item.substr(0, colon);
        const size_t plus = when.find('+');
        e.frame = std::atoi(when.substr(0, plus).c_str());
        e.length = plus == std::string::npos ? 0 : std::atoi(when.substr(plus + 1).c_str());
        if (!parse_buttons(item.substr(colon + 1), e.buttons)) return false;
        events.push_back(e);
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const Event& a, const Event& b) { return a.frame < b.frame; });
    return true;
}

void usage()
{
    std::fprintf(stderr,
                 "usage: arcade_host <game> <zip|file|dir>... [--frames N] [--png-every K] [--out DIR]\n"
                 "                   [--wav FILE] [--rate HZ] [--dips HEX] [--input SCRIPT] [--verbose]\n"
                 "games: pacman\n");
}

double ms_since(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) { usage(); return 2; }
    const std::string game = argv[1];
    if (game != "pacman") {
        std::fprintf(stderr, "unknown game '%s'\n", game.c_str());
        usage();
        return 2;
    }
    const romset::Set& set = pacman::kRomSet;

    int frames = 600, png_every = 0, dips = -1, verbose = 0;
    uint32_t rate = 24000;
    std::string out_dir = ".", wav_path, script;
    std::vector<std::string> paths;
    for (int a = 2; a < argc; ++a) {
        const std::string arg = argv[a];
        auto value = [&](const char* opt) -> std::string {
            if (a + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", opt); std::exit(2); }
            return argv[++a];
        };
        if (arg == "--frames") frames = std::atoi(value("--frames").c_str());
        else if (arg == "--png-every") png_every = std::atoi(value("--png-every").c_str());
        else if (arg == "--out") out_dir = value("--out");
        else if (arg == "--wav") wav_path = value("--wav");
        else if (arg == "--rate") rate = uint32_t(std::atoi(value("--rate").c_str()));
        else if (arg == "--dips") dips = int(std::strtol(value("--dips").c_str(), nullptr, 16));
        else if (arg == "--input") script = value("--input");
        else if (arg == "--verbose") verbose = 1;
        else if (arg.rfind("--", 0) == 0) { std::fprintf(stderr, "unknown option %s\n", arg.c_str()); return 2; }
        else paths.push_back(arg);
    }
    if (rate < 8000 || rate > 96000) { std::fprintf(stderr, "--rate out of range\n"); return 2; }

    std::vector<Event> events;
    if (!parse_script(script, events)) return 2;

    // --- ROMs ---
    std::vector<File> files;
    for (const std::string& p : paths) {
        if (is_dir(p)) {
            add_dir(p, set, files);
        } else {
            File f;
            f.name = p;
            if (!read_file(p, f.data)) { std::fprintf(stderr, "can't read %s\n", p.c_str()); return 2; }
            files.push_back(std::move(f));
        }
    }
    std::vector<romset::Source> sources;
    for (const File& f : files) sources.push_back({f.name.c_str(), f.data.data(), f.data.size()});
    std::vector<std::vector<uint8_t>> regions(size_t(set.region_count));
    std::vector<uint8_t*> region_ptrs;
    for (int r = 0; r < set.region_count; ++r) {
        regions[size_t(r)].resize(set.regions[r].size);
        region_ptrs.push_back(regions[size_t(r)].data());
    }
    std::vector<romset::Status> status(size_t(set.rom_count));
    const bool all = romset::load(set, sources.data(), int(sources.size()), region_ptrs.data(), status.data());
    char msg[4096];
    const int bad = romset::report(set, status.data(), msg, sizeof msg);
    if (msg[0]) std::fputs(msg, all ? stdout : stderr);
    if (!all) {
        std::fprintf(stderr, "%s: %d ROM(s) missing or wrong in %zu file(s) given\n", set.game, bad,
                     files.size());
        return 1;
    }
    std::printf("%s: all ROMs found (%zu files searched)\n", set.title, files.size());

    // --- run ---
    static pacman::Machine m;
    m.init({regions[pacman::kRegionCpu].data(), regions[pacman::kRegionGfx].data(),
            regions[pacman::kRegionProms].data(), regions[pacman::kRegionSound].data()},
           rate);
    if (dips >= 0) m.set_dips(uint8_t(dips));

    writers::Wav wav;
    if (!wav_path.empty() && !wav.open(wav_path.c_str(), rate)) {
        std::fprintf(stderr, "can't write %s\n", wav_path.c_str());
        return 2;
    }

    std::vector<uint16_t> frame(size_t(pacman::kScreenW) * pacman::kScreenH);
    int16_t audio[2048];
    uint16_t held = 0;
    std::vector<std::pair<int, uint16_t>> releases;     // (frame, buttons)
    size_t next_event = 0;
    double emu_sum = 0, emu_max = 0, ren_sum = 0, ren_max = 0;
    double sec_emu = 0, sec_ren = 0, sec_emu_max = 0, sec_ren_max = 0;
    const double frame_ms = 1000.0 * pacman::kCyclesPerFrame / pacman::kCpuClock;
    const int fps = 60;

    for (int f = 0; f < frames; ++f) {
        // Inputs for this frame.
        for (auto it = releases.begin(); it != releases.end();) {
            if (it->first <= f) { held = uint16_t(held & ~it->second); it = releases.erase(it); }
            else ++it;
        }
        while (next_event < events.size() && events[next_event].frame <= f) {
            const Event& e = events[next_event++];
            if (e.length > 0) {
                held |= e.buttons;
                releases.push_back({e.frame + e.length, e.buttons});
            } else {
                held = e.buttons;
            }
        }
        m.set_inputs(held);

        auto t0 = Clock::now();
        const int n = m.run_frame(audio, int(sizeof audio / sizeof audio[0]));
        const double emu = ms_since(t0);
        wav.write(audio, n);

        t0 = Clock::now();
        for (int y = 0; y < pacman::kScreenH; ++y) m.render_line(y, frame.data() + y * pacman::kScreenW);
        const double ren = ms_since(t0);

        emu_sum += emu; ren_sum += ren;
        emu_max = std::max(emu_max, emu); ren_max = std::max(ren_max, ren);
        sec_emu += emu; sec_ren += ren;
        sec_emu_max = std::max(sec_emu_max, emu); sec_ren_max = std::max(sec_ren_max, ren);
        if (verbose)
            std::printf("frame %5d  emu %.3f ms  render %.3f ms  audio %d  inputs %03x\n", f, emu, ren, n, held);
        if ((f + 1) % fps == 0) {
            std::printf("frames %5d-%5d  emu avg %.3f max %.3f ms  render avg %.3f max %.3f ms\n",
                        f + 1 - fps, f, sec_emu / fps, sec_emu_max, sec_ren / fps, sec_ren_max);
            sec_emu = sec_ren = sec_emu_max = sec_ren_max = 0;
        }
        if (png_every > 0 && f % png_every == 0) {
            char path[1024];
            std::snprintf(path, sizeof path, "%s/%s_%05d.png", out_dir.c_str(), set.game, f);
            if (!writers::write_png_rgb565(path, frame.data(), pacman::kScreenW, pacman::kScreenH))
                std::fprintf(stderr, "can't write %s\n", path);
        }
    }
    wav.close();
    if (frames > 0)
        std::printf("summary: %d frames (%.1f s of game), emu avg %.3f ms (max %.3f), render avg %.3f ms "
                    "(max %.3f) per %.2f ms frame; watchdog resets %d\n",
                    frames, frames * frame_ms / 1000.0, emu_sum / frames, emu_max, ren_sum / frames,
                    ren_max, frame_ms, m.watchdog_resets());
    return 0;
}
