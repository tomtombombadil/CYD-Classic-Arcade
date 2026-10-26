// Headless runner for the emulated machines (host only, MIT).
//
//   arcade_host <game> <zip | rom file | dir>... [options]
//     games: the table in emu/games.cpp (pacman, invaders, rallyx, dkong,
//     galaga, digdug, mspacman, mrdo, btime, qbert, asteroid, tempest,
//     robotron, defender, qix, starwars, missile, bzone, zaxxon,
//     ...)
//     --frames N          frames to run (default 600)
//     --png-every K       write every K-th frame as PNG (0 = none; default 0)
//     --out DIR           folder for the PNGs (default ".")
//     --wav FILE          write the whole run's audio
//     --rate HZ           audio rate (default 24000)
//     --dips HEX          DIP switch bank (default: the machine's default)
//     --demo-sounds on|off  the attract mode's sounds switch
//                         (Machine::set_demo_sounds, after --dips; default:
//                         as the machine starts)
//     --samples ZIP|DIR   the game's MAME sample set (WAVs), if it has one
//     --input SCRIPT      e.g. "120+6:coin,200+6:start1,400:left,460:"
//                         F:btns   from frame F hold exactly btns ('+'-joined;
//                                  empty or "none" = release all)
//                         F+N:btns press btns at frame F for N frames
//                         buttons: per game (pacman: up down left right coin
//                         coin2 start1 start2 service test; invaders: coin
//                         start1 start2 left right fire left2 right2 fire2;
//                         rallyx: up down left right smoke coin coin2 start1
//                         start2 service;
//                         dkong, dkongjr: up down left right jump coin start1 start2;
//                         tempest: fire zap left right coin coin2 start1
//                         start2 test step;
//                         galaga, digdug: up down left right fire coin coin2
//                         start1 start2 service test; mspacman: as pacman;
//                         mrdo: up down left right fire coin coin2 start1
//                         start2 tilt;
//                         btime: up down left right pepper coin coin2 start1
//                         start2;
//                         qbert: upright downright downleft upleft coin
//                         coin2 start1 start2 test select; asteroid: left
//                         right thrust fire hyperspace coin coin2 start1 start2
//                         test step;
//                         robotron: up down left right fireup firedown
//                         fireleft fireright coin coin2 start1 start2
//                         advance autoup hsreset;
//                         defender: up down left right (the stick: reverse /
//                         thrust by the ship's facing) fire thrust bomb hyper
//                         reverse coin start1 start2 advance autoup hsreset;
//                         qix: up down left right fast slow coin coin2
//                         start1 start2 advance;
//                         starwars: fire fire2 fire3 fire4 up down left right
//                         (the sights) coin coin2 aux start1 (= fire) test
//                         step;
//                         missile: up down left right (the trackball, steady)
//                         fireleft fire fireright coin coin2 start1 start2
//                         test tilt; bzone: fire lfwd lback rfwd rback (the
//                         treads) up down left right (both treads, as the
//                         d-pad) coin coin2 start1 start2 test step;
//                         zaxxon: up (= dive) down left right fire
//                         coin coin2 start1 start2 service test;
//                         galaxian: left right fire coin coin2
//                         start1 start2 service test; amidar: up down left
//                         right jump coin coin2 start1 start2 service;
//                         digdug2: up down left right pump hammer coin
//                         coin2 start1 start2 service test;
//                         elevator: up down left right fire jump coin
//                         coin2 start1 start2 service tilt;
//                         gyruss: up down left right fire coin coin2 start1
//                         start2 service;
//                         1942: up down left right fire loop coin coin2
//                         start1 start2 service;
//                         any game: lx=N / ly=N, the left stick's X / Y
//                         -512..511 (down positive), e.g.
//                         "1200+60:lx=-300+fire")
//     --verbose           one timing line per frame
//     --hash FILE         one line per frame: CRC-32 of its picture and of
//                         its audio (to check that a change leaves the
//                         emulation exactly as it was)
//     --dump DIR          for tools/xtensa_sim: writes the ROM regions
//                         (DIR/r0.bin ...) and each frame's inputs
//                         (DIR/inputs.bin: u16 buttons, i16 stick X, Y, 0)
//                         for --frames frames, and stops (nothing is run)
//
//   arcade_host --check-table
//     checks the game table against the firmware's limits (ROM regions)
//
//   arcade_host --list-json
//     prints every game's ROM set as JSON (the web page's ROM loader reads
//     it to pick out the zips worth sending to the board)
//
// Prints per-second timing (emulation and line rendering, ms per frame) and
// a summary, so the cost on the device can be estimated.
#include "../../emu/games.h"
#include "../../emu/rom/crc32.h"
#include "../../emu/rom/romset.h"
#include "../../emu/rom/zip.h"
#include "../../emu/sound/samples.h"
#include "writers.h"
#include "../../main/line_diff.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
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

using GameInfo = games::Info;

// The last path component equals `name`, ignoring case.
bool same_name(const char* path, size_t len, const std::string& name)
{
    size_t start = len;
    while (start > 0 && path[start - 1] != '/' && path[start - 1] != '\\') --start;
    return lower(std::string(path + start, len - start)) == lower(name);
}

// Reads the game's sample WAVs from a ZIP or a folder into `bufs` and
// parses them into `wavs` (one per sample name; missing ones stay empty).
int load_samples(const std::string& path, const GameInfo& g, std::vector<std::vector<uint8_t>>& bufs,
                 std::vector<samples::Wav>& wavs)
{
    bufs.assign(size_t(g.sample_count), {});
    wavs.assign(size_t(g.sample_count), samples::Wav{});
    std::vector<uint8_t> zip_data;
    zip::Reader z;
    const bool is_zip = !is_dir(path) && read_file(path, zip_data) &&
                        zip::looks_like_zip(zip_data.data(), zip_data.size()) &&
                        z.open(zip_data.data(), zip_data.size());
    int found = 0;
    for (int i = 0; i < g.sample_count; ++i) {
        const std::string file = std::string(g.sample_names[i]) + ".wav";
        std::vector<uint8_t>& buf = bufs[size_t(i)];
        if (is_zip) {
            uint32_t it = z.begin();
            zip::Entry e;
            while (z.next(it, e)) {
                if (!same_name(e.name, e.name_len, file)) continue;
                buf.resize(e.size);
                if (!z.extract(e, buf.data())) buf.clear();
                break;
            }
        } else if (is_dir(path)) {
            read_file(path + "/" + file, buf);
        }
        if (buf.empty()) continue;
        if (samples::parse_wav(buf.data(), buf.size(), wavs[size_t(i)])) ++found;
        else std::fprintf(stderr, "sample %s: not a WAV this player reads\n", file.c_str());
    }
    return found;
}

// A folder: the game's, its parent's and its variants' ZIPs, plus every
// small loose file (ROM images). Other ZIPs only when the folder is small.
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
        bool own = ln == game_zip || ln == parent_zip;
        for (int v = 0; v < set.variant_count; ++v) own = own || ln == std::string(set.variants[v]) + ".zip";
        if (zip) take = own || (names.size() < 64 && st.st_size < (4 << 20));
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
    int      stick = 0;  // the left stick's X (lx=N), -512..511, while this holds
    int      stick_y = 0;  // ... its Y (ly=N, down positive)
};

bool parse_buttons(const GameInfo& g, const std::string& s, uint16_t& out, int& stick, int& stick_y)
{
    out = 0;
    stick = 0;
    stick_y = 0;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find_first_of("+|", pos);
        if (end == std::string::npos) end = s.size();
        const std::string name = lower(s.substr(pos, end - pos));
        bool found = name == "none";
        if (name.rfind("lx=", 0) == 0) {
            stick = std::max(-512, std::min(511, std::atoi(name.c_str() + 3)));
            found = true;
        }
        if (name.rfind("ly=", 0) == 0) {
            stick_y = std::max(-512, std::min(511, std::atoi(name.c_str() + 3)));
            found = true;
        }
        for (int k = 0; k < g.button_count; ++k)
            if (name == g.buttons[k].name) { out |= g.buttons[k].bit; found = true; }
        if (!found && !name.empty()) {
            std::fprintf(stderr, "unknown button '%s'\n", name.c_str());
            return false;
        }
        pos = end + 1;
    }
    return true;
}

bool parse_script(const GameInfo& g, const std::string& script, std::vector<Event>& events)
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
        if (!parse_buttons(g, item.substr(colon + 1), e.buttons, e.stick, e.stick_y)) return false;
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
                 "                   [--wav FILE] [--rate HZ] [--dips HEX] [--samples ZIP|DIR]\n"
                 "                   [--demo-sounds on|off] [--input SCRIPT] [--verbose] [--hash FILE] [--panel WxH]\n"
                 "  --panel WxH: draw as the firmware does (changed segments only, scaled, pictures\n"
                 "               cut short now and then) onto a panel of that size, and check every\n"
                 "               finished picture against a full redraw; prints what was sent\n"
                 "games:");
    for (int i = 0; i < games::kGameCount; ++i) std::fprintf(stderr, " %s", games::kGames[i].id);
    std::fprintf(stderr, "\n");
}

double ms_since(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

} // namespace

// Every game's ROM set as JSON, for web/roms/.
int list_json()
{
    std::printf("[\n");
    for (int g = 0; g < games::kGameCount; ++g) {
        const games::Info& gi = games::kGames[g];
        const romset::Set& set = *gi.set;
        std::printf("  {\"id\": \"%s\", \"title\": \"%s\", \"parent\": %s%s%s, ", gi.id, gi.title,
                    set.parent ? "\"" : "", set.parent ? set.parent : "null", set.parent ? "\"" : "");
        // Alternative sets (e.g. dkong / dkongo): a ROM with "variant" is
        // needed only for that set; the others are needed for every one.
        if (set.variant_count > 0) {
            std::printf("\"variants\": [");
            for (int v = 0; v < set.variant_count; ++v)
                std::printf("%s\"%s\"", v ? ", " : "", set.variants[v]);
            std::printf("], ");
        }
        std::printf("\"roms\": [\n");
        for (int r = 0; r < set.rom_count; ++r) {
            const romset::Rom& rom = set.roms[r];
            std::printf("    {\"name\": \"%s\", \"size\": %u, \"crc\": \"%08x\", \"optional\": %s", rom.name,
                        unsigned(rom.size), unsigned(rom.crc), rom.optional ? "true" : "false");
            if (rom.variant > 0 && rom.variant <= set.variant_count)
                std::printf(", \"variant\": \"%s\"", set.variants[rom.variant - 1]);
            std::printf("}%s\n", r + 1 < set.rom_count ? "," : "");
        }
        std::printf("  ]}%s\n", g + 1 < games::kGameCount ? "," : "");
    }
    std::printf("]\n");
    return 0;
}

// Checks the game table against the firmware's limits: the ROM loader holds
// romset::kMaxRegions regions (v0.6.0's Dig Dug had 9 of 8 and wouldn't
// load on the board). Prints each game's ROM RAM.
int check_table()
{
    int bad = 0;
    for (int g = 0; g < games::kGameCount; ++g) {
        const romset::Set& set = *games::kGames[g].set;
        size_t bytes = 0;
        for (int r = 0; r < set.region_count; ++r) bytes += set.regions[r].size;
        std::printf("%-10s %2d regions, %6zu bytes of ROM in RAM\n", games::kGames[g].id, set.region_count, bytes);
        if (set.region_count > romset::kMaxRegions) {
            std::printf("  too many regions: the loader holds %d\n", romset::kMaxRegions);
            ++bad;
        }
    }
    if (games::kGameCount > games::kMaxGames) {
        std::printf("%d games: the firmware's catalog holds %d\n", games::kGameCount, games::kMaxGames);
        ++bad;
    }
    return bad ? 1 : 0;
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--list-json") == 0) return list_json();
    if (argc == 2 && std::strcmp(argv[1], "--check-table") == 0) return check_table();
    if (argc < 3) { usage(); return 2; }
    const std::string game = argv[1];
    const GameInfo* info = nullptr;
    info = games::find(game.c_str());
    if (!info) {
        std::fprintf(stderr, "unknown game '%s'\n", game.c_str());
        usage();
        return 2;
    }
    const GameInfo& gi = *info;
    const romset::Set& set = *gi.set;

    int frames = 600, png_every = 0, dips = -1, demo_sounds = -1, verbose = 0, panel_w = 0, panel_h = 0;
    uint32_t rate = 24000;
    std::string out_dir = ".", wav_path, script, samples_path, hash_path, dump_dir;
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
        else if (arg == "--demo-sounds") {
            const std::string v = value("--demo-sounds");
            if (v == "on") demo_sounds = 1;
            else if (v == "off") demo_sounds = 0;
            else { std::fprintf(stderr, "--demo-sounds on|off\n"); return 2; }
        }
        else if (arg == "--input") script = value("--input");
        else if (arg == "--samples") samples_path = value("--samples");
        else if (arg == "--verbose") verbose = 1;
        else if (arg == "--hash") hash_path = value("--hash");
        else if (arg == "--dump") dump_dir = value("--dump");
        else if (arg == "--panel") {
            const std::string v = value("--panel");
            if (std::sscanf(v.c_str(), "%dx%d", &panel_w, &panel_h) != 2 || panel_w < 64 || panel_h < 64) {
                std::fprintf(stderr, "--panel WxH, e.g. 320x480\n");
                return 2;
            }
        }
        else if (arg.rfind("--", 0) == 0) { std::fprintf(stderr, "unknown option %s\n", arg.c_str()); return 2; }
        else paths.push_back(arg);
    }
    if (rate < 8000 || rate > 96000) { std::fprintf(stderr, "--rate out of range\n"); return 2; }

    std::vector<Event> events;
    if (!parse_script(gi, script, events)) return 2;

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
    const int variant = romset::pick_variant(set, sources.data(), int(sources.size()));
    if (variant > 0) std::printf("%s: all ROMs found, set %s (%zu files searched)\n", set.title, set.variants[variant - 1], files.size());
    else             std::printf("%s: all ROMs found (%zu files searched)\n", set.title, files.size());

    FILE* dump_inputs = nullptr;
    if (!dump_dir.empty()) {
        for (int r = 0; r < set.region_count; ++r) {
            const std::string path = dump_dir + "/r" + std::to_string(r) + ".bin";
            FILE* o = std::fopen(path.c_str(), "wb");
            if (!o || std::fwrite(region_ptrs[size_t(r)], 1, set.regions[r].size, o) != set.regions[r].size) {
                std::fprintf(stderr, "can't write %s\n", path.c_str());
                return 2;
            }
            std::fclose(o);
        }
        if (!(dump_inputs = std::fopen((dump_dir + "/inputs.bin").c_str(), "wb"))) {
            std::fprintf(stderr, "can't write %s/inputs.bin\n", dump_dir.c_str());
            return 2;
        }
    }

    // --- run ---
    std::unique_ptr<games::Machine> m(gi.make());
    if (!m) {
        std::fprintf(stderr, "%s: not enough memory for the machine\n", gi.id);
        return 1;
    }
    std::vector<const uint8_t*> rom_ptrs(region_ptrs.begin(), region_ptrs.end());
    m->init(rom_ptrs.data(), rate, false);
    if (dips >= 0) m->set_dips(uint8_t(dips));
    if (demo_sounds >= 0) {
        const bool has = m->set_demo_sounds(demo_sounds != 0);
        std::printf("demo sounds %s%s\n", demo_sounds ? "on" : "off",
                    has ? "" : ": this game has no such switch (nothing changed)");
    }

    std::vector<std::vector<uint8_t>> sample_bufs;
    std::vector<samples::Wav> wavs;
    if (!samples_path.empty()) {
        if (!gi.sample_count) {
            std::fprintf(stderr, "%s has no sample set\n", gi.id);
            return 2;
        }
        const int found = load_samples(samples_path, gi, sample_bufs, wavs);
        std::printf("samples: %d of %d found in %s\n", found, gi.sample_count, samples_path.c_str());
        std::vector<const samples::Wav*> ptrs;
        for (const samples::Wav& w : wavs) ptrs.push_back(w.frames ? &w : nullptr);
        m->set_samples(ptrs.data());
    }

    writers::Wav wav;
    if (!wav_path.empty() && !wav.open(wav_path.c_str(), rate)) {
        std::fprintf(stderr, "can't write %s\n", wav_path.c_str());
        return 2;
    }

    FILE* hash_file = nullptr;
    if (!hash_path.empty() && !(hash_file = std::fopen(hash_path.c_str(), "w"))) {
        std::fprintf(stderr, "can't write %s\n", hash_path.c_str());
        return 2;
    }

    std::vector<uint16_t> frame(size_t(gi.width) * gi.height);

    // --panel: the firmware's drawing (main/game_runner.cpp) onto a panel
    // in memory, sizes and scaling as there; a landscape game turns it
    struct Panel {
        int w = 0, h = 0, out_w = 0, out_h = 0;
        std::vector<uint16_t> px, want, x_map, src, strip;
        std::vector<uint32_t> seg_hash;
        line_diff::Drawer d;
        bool async = false, shot_pending = false;
        int shot_wait = 0;
        std::vector<uint16_t> shot_frame;
        long long pictures = 0, finished = 0, cut = 0, blocks = 0, pixels = 0, line_pixels = 0, bad = 0;
        uint32_t rnd = 12345;
        uint32_t next() { rnd = rnd * 1103515245u + 12345u; return rnd >> 16; }
    } panel;
    if (panel_w) {
        Panel& p = panel;
        p.w = panel_w;
        p.h = panel_h;
        if ((gi.orientation == games::Orientation::Landscape) != (p.w > p.h)) std::swap(p.w, p.h);
        const float scale = std::max(1.0f, std::min(float(p.w) / gi.width, float(p.h) / gi.height));
        p.out_w = std::min(p.w, int(gi.width * scale + 0.5f));
        p.out_h = std::min(p.h, int(gi.height * scale + 0.5f));
        p.px.assign(size_t(p.out_w) * p.out_h, 0);
        p.want.resize(p.px.size());
        p.x_map.resize(size_t(p.out_w));
        for (int x = 0; x < p.out_w; ++x) p.x_map[size_t(x)] = uint16_t(x * gi.width / p.out_w);
        p.src.resize(size_t(gi.width));
        p.strip.resize(size_t(p.out_w) * 8);
        p.d.segs = line_diff::layout(gi.width);
        p.seg_hash.resize(size_t(gi.height) * size_t(p.d.segs.segs));
        p.d.height = gi.height;
        p.d.out_w = p.out_w;
        p.d.out_h = p.out_h;
        p.d.scaled = p.out_w != gi.width || p.out_h != gi.height;
        p.d.x_map = p.x_map.data();
        p.d.src = p.src.data();
        p.d.strip = p.strip.data();
        p.d.strip_rows = 8;
        p.d.seg_hash = p.seg_hash.data();
        p.d.setup();
        p.async = m->snapshot();   // drawn on another core in the firmware
        std::printf("%s", p.async ? "panel: drawn from snapshots (another core)\n" : "");
        std::printf("panel %dx%d: %dx%d shown as %dx%d, %d segments of %d pixels\n", p.w, p.h, gi.width,
                    gi.height, p.out_w, p.out_h, p.d.segs.segs, p.d.segs.seg_w);
    }
    int16_t audio[2048];
    uint16_t held = 0;
    int stick = 0, stick_y = 0;
    std::vector<std::pair<int, uint16_t>> releases;     // (frame, buttons)
    int stick_until = -1;                               // a pressed stick lets go then
    size_t next_event = 0;
    double emu_sum = 0, emu_max = 0, ren_sum = 0, ren_max = 0;
    double sec_emu = 0, sec_ren = 0, sec_emu_max = 0, sec_ren_max = 0;
    const double frame_ms = 1000.0 * gi.cycles_per_frame / gi.cpu_clock;
    const int fps = 60;

    for (int f = 0; f < frames; ++f) {
        // Inputs for this frame.
        for (auto it = releases.begin(); it != releases.end();) {
            if (it->first <= f) { held = uint16_t(held & ~it->second); it = releases.erase(it); }
            else ++it;
        }
        if (stick_until >= 0 && stick_until <= f) { stick = stick_y = 0; stick_until = -1; }
        while (next_event < events.size() && events[next_event].frame <= f) {
            const Event& e = events[next_event++];
            if (e.length > 0) {
                held |= e.buttons;
                releases.push_back({e.frame + e.length, e.buttons});
                if (e.stick || e.stick_y) {
                    stick = e.stick;
                    stick_y = e.stick_y;
                    stick_until = e.frame + e.length;
                }
            } else {
                held = e.buttons;
                stick = e.stick;
                stick_y = e.stick_y;
                stick_until = -1;
            }
        }
        m->set_inputs(held);
        games::Analog analog;
        analog.lx = int16_t(stick);
        analog.ly = int16_t(stick_y);
        m->set_analog(analog);
        if (dump_inputs) {
            const int16_t rec[4] = {int16_t(held), analog.lx, analog.ly, 0};
            std::fwrite(rec, 2, 4, dump_inputs);
            continue;
        }

        auto t0 = Clock::now();
        const int n = m->run_frame(audio, int(sizeof audio / sizeof audio[0]));
        const double emu = ms_since(t0);
        wav.write(audio, n);

        t0 = Clock::now();
        for (int y = 0; y < gi.height; ++y) m->render_line(y, frame.data() + y * gi.width);
        const double ren = ms_since(t0);

        if (hash_file)
            std::fprintf(hash_file, "%d %08x %08x %d\n", f,
                         unsigned(crc32::of(reinterpret_cast<const uint8_t*>(frame.data()), frame.size() * 2)),
                         unsigned(crc32::of(reinterpret_cast<const uint8_t*>(audio), size_t(n) * 2)), n);
        if (panel_w) {
            Panel& p = panel;
            // One picture: `exp` is the full picture it must leave on the panel
            auto picture = [&](bool from_shot, const std::vector<uint16_t>& exp, bool may_cut) {
                ++p.pictures;
                // what v0.14 sent: every changed line in full
                for (int r = 0; r < p.out_h; ++r) {
                    const uint16_t* gl = exp.data() + size_t(r * gi.height / p.out_h) * gi.width;
                    const uint16_t* row = &p.px[size_t(r) * p.out_w];
                    for (int x = 0; x < p.out_w; ++x)
                        if (row[x] != gl[p.x_map[size_t(x)]]) {
                            p.line_pixels += p.out_w;
                            break;
                        }
                }
                const bool done = p.d.frame(
                    [&](int y, uint16_t* line) {
                        if (from_shot) m->render_snapshot_line(y, line);
                        else m->render_line(y, line);
                    },
                    [&](int x, int y, int w, int h, const uint16_t* px) {
                        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > p.out_w || y + h > p.out_h || h > 8) {
                            std::fprintf(stderr, "frame %d: block %d,%d %dx%d off the picture\n", f, x, y, w, h);
                            ++p.bad;
                            return;
                        }
                        for (int r = 0; r < h; ++r)
                            std::memcpy(&p.px[size_t(y + r) * p.out_w + x], px + r * w, size_t(w) * 2);
                        ++p.blocks;
                        p.pixels += w * h;
                    },
                    [&]() { return may_cut && p.next() % 64 == 0; });
                if (!done) {
                    ++p.cut;
                    return;
                }
                ++p.finished;
                for (int r = 0; r < p.out_h; ++r) {
                    const uint16_t* gl = exp.data() + size_t(r * gi.height / p.out_h) * gi.width;
                    for (int x = 0; x < p.out_w; ++x) p.want[size_t(r) * p.out_w + x] = gl[p.x_map[size_t(x)]];
                }
                if (p.px != p.want) {
                    ++p.bad;
                    if (p.bad <= 5) {
                        int nd = 0, fx = -1, fy = -1;
                        for (size_t i = 0; i < p.px.size(); ++i)
                            if (p.px[i] != p.want[i] && nd++ == 0) {
                                fx = int(i % size_t(p.out_w));
                                fy = int(i / size_t(p.out_w));
                            }
                        std::fprintf(stderr, "frame %d: panel differs from the full picture: %d pixels, first at %d,%d\n",
                                     f, nd, fx, fy);
                    }
                }
            };
            if (p.async) {
                // As the firmware's other core does: the snapshot of an
                // earlier frame is drawn after later frames have run (one
                // or two), so it must not depend on the machine's state now
                if (p.shot_pending && --p.shot_wait <= 0) {
                    picture(true, p.shot_frame, false);
                    p.shot_pending = false;
                }
                if (!p.shot_pending && p.next() % 4 != 0) {
                    if (!m->snapshot()) {
                        std::fprintf(stderr, "frame %d: snapshot failed\n", f);
                        ++p.bad;
                    }
                    p.shot_frame = frame;
                    p.shot_pending = true;
                    p.shot_wait = 1 + int(p.next() % 2);
                }
            } else if (p.next() % 4 != 0) {    // the firmware skips some pictures
                picture(false, frame, p.next() % 5 == 0);
            }
        }
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
            if (!writers::write_png_rgb565(path, frame.data(), gi.width, gi.height))
                std::fprintf(stderr, "can't write %s\n", path);
        }
    }
    wav.close();
    if (hash_file) std::fclose(hash_file);
    if (dump_inputs) {
        std::fclose(dump_inputs);
        std::printf("dumped %d regions and %d frames of inputs to %s\n", set.region_count, frames, dump_dir.c_str());
        return 0;
    }
    if (panel_w) {
        const Panel& p = panel;
        std::printf("panel: %lld pictures (%lld finished, %lld cut short), %lld blocks, %.0f pixels a picture "
                    "(%.1f %% of the panel; whole changed lines: %.0f); %lld wrong\n",
                    p.pictures, p.finished, p.cut, p.blocks, p.pictures ? double(p.pixels) / p.pictures : 0.0,
                    p.pictures ? 100.0 * double(p.pixels) / p.pictures / (double(p.out_w) * p.out_h) : 0.0,
                    p.pictures ? double(p.line_pixels) / p.pictures : 0.0, p.bad);
        if (p.bad) return 1;
    }
    if (frames > 0)
        std::printf("summary: %d frames (%.1f s of game), emu avg %.3f ms (max %.3f), render avg %.3f ms "
                    "(max %.3f) per %.2f ms frame; watchdog resets %d\n",
                    frames, frames * frame_ms / 1000.0, emu_sum / frames, emu_max, ren_sum / frames,
                    ren_max, frame_ms, m->watchdog_resets());
    return 0;
}
