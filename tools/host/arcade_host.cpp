// Headless runner for the emulated machines (host only, MIT).
//
//   arcade_host <game> <zip | rom file | dir>... [options]
//     games: pacman, invaders (the table `kGames` below)
//     --frames N          frames to run (default 600)
//     --png-every K       write every K-th frame as PNG (0 = none; default 0)
//     --out DIR           folder for the PNGs (default ".")
//     --wav FILE          write the whole run's audio
//     --rate HZ           audio rate (default 24000)
//     --dips HEX          DIP switch bank (default: the machine's default)
//     --samples ZIP|DIR   the game's MAME sample set (WAVs), if it has one
//     --input SCRIPT      e.g. "120+6:coin,200+6:start1,400:left,460:"
//                         F:btns   from frame F hold exactly btns ('+'-joined;
//                                  empty or "none" = release all)
//                         F+N:btns press btns at frame F for N frames
//                         buttons: per game (pacman: up down left right coin
//                         coin2 start1 start2 service test; invaders: coin
//                         start1 start2 left right fire left2 right2 fire2)
//     --verbose           one timing line per frame
//
// Prints per-second timing (emulation and line rendering, ms per frame) and
// a summary, so the cost on the device can be estimated.
#include "../../emu/machines/invaders.h"
#include "../../emu/machines/pacman.h"
#include "../../emu/rom/romset.h"
#include "../../emu/rom/zip.h"
#include "../../emu/sound/samples.h"
#include "writers.h"

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

// --- the games ---------------------------------------------------------------

struct ButtonName {
    const char* name;
    uint16_t    bit;
};

// One emulated machine behind a common face, so the runner below works for
// every game in kGames.
class Game {
public:
    virtual ~Game() = default;
    virtual void init(uint8_t* const* regions, uint32_t rate) = 0;
    virtual void set_dips(uint8_t dips) = 0;
    virtual void set_inputs(uint16_t buttons) = 0;
    virtual void set_samples(const samples::Wav* const*) {}
    virtual int  run_frame(int16_t* audio, int cap) = 0;
    virtual void render_line(int y, uint16_t* out) = 0;
    virtual int  watchdog_resets() const = 0;
};

struct GameInfo {
    const char*        name;
    const romset::Set* set;
    int                width, height;
    uint32_t           cpu_clock;
    int                cycles_per_frame;
    const ButtonName*  buttons;
    int                button_count;
    const char* const* sample_names;    // the MAME sample set's files (without .wav), or null
    int                sample_count;
    std::unique_ptr<Game> (*make)();
};

class PacmanGame : public Game {
public:
    void init(uint8_t* const* r, uint32_t rate) override
    {
        m_.init({r[pacman::kRegionCpu], r[pacman::kRegionGfx], r[pacman::kRegionProms], r[pacman::kRegionSound]}, rate);
    }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    pacman::Machine m_;
};

class InvadersGame : public Game {
public:
    void init(uint8_t* const* r, uint32_t rate) override { m_.init({r[invaders::kRegionCpu]}, rate); }
    void set_dips(uint8_t d) override { m_.set_dips(d); }
    void set_inputs(uint16_t b) override { m_.set_inputs(b); }
    void set_samples(const samples::Wav* const* w) override { m_.set_samples(w); }
    int  run_frame(int16_t* a, int cap) override { return m_.run_frame(a, cap); }
    void render_line(int y, uint16_t* out) override { m_.render_line(y, out); }
    int  watchdog_resets() const override { return m_.watchdog_resets(); }

private:
    invaders::Machine m_;
};

const ButtonName kPacmanButtons[] = {
    {"up", pacman::kUp}, {"down", pacman::kDown}, {"left", pacman::kLeft},
    {"right", pacman::kRight}, {"coin", pacman::kCoin}, {"coin2", pacman::kCoin2},
    {"start1", pacman::kStart1}, {"start2", pacman::kStart2},
    {"service", pacman::kService}, {"test", pacman::kTest},
};

const ButtonName kInvadersButtons[] = {
    {"coin", invaders::kCoin}, {"start1", invaders::kStart1}, {"start2", invaders::kStart2},
    {"left", invaders::kLeft}, {"right", invaders::kRight}, {"fire", invaders::kFire},
    {"left2", invaders::kLeft2}, {"right2", invaders::kRight2}, {"fire2", invaders::kFire2},
};

template <class G> std::unique_ptr<Game> make_game() { return std::unique_ptr<Game>(new G); }

const GameInfo kGames[] = {
    {"pacman", &pacman::kRomSet, pacman::kScreenW, pacman::kScreenH, pacman::kCpuClock,
     pacman::kCyclesPerFrame, kPacmanButtons, int(sizeof kPacmanButtons / sizeof kPacmanButtons[0]),
     nullptr, 0, make_game<PacmanGame>},
    {"invaders", &invaders::kRomSet, invaders::kScreenW, invaders::kScreenH, invaders::kCpuClock,
     invaders::kCyclesPerFrame, kInvadersButtons, int(sizeof kInvadersButtons / sizeof kInvadersButtons[0]),
     invaders::kSampleNames, invaders::kSampleCount, make_game<InvadersGame>},
};

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

bool parse_buttons(const GameInfo& g, const std::string& s, uint16_t& out)
{
    out = 0;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find_first_of("+|", pos);
        if (end == std::string::npos) end = s.size();
        const std::string name = lower(s.substr(pos, end - pos));
        bool found = name == "none";
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
        if (!parse_buttons(g, item.substr(colon + 1), e.buttons)) return false;
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
                 "                   [--input SCRIPT] [--verbose]\n"
                 "games:");
    for (const GameInfo& g : kGames) std::fprintf(stderr, " %s", g.name);
    std::fprintf(stderr, "\n");
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
    const GameInfo* info = nullptr;
    for (const GameInfo& g : kGames)
        if (game == g.name) info = &g;
    if (!info) {
        std::fprintf(stderr, "unknown game '%s'\n", game.c_str());
        usage();
        return 2;
    }
    const GameInfo& gi = *info;
    const romset::Set& set = *gi.set;

    int frames = 600, png_every = 0, dips = -1, verbose = 0;
    uint32_t rate = 24000;
    std::string out_dir = ".", wav_path, script, samples_path;
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
        else if (arg == "--samples") samples_path = value("--samples");
        else if (arg == "--verbose") verbose = 1;
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
    std::printf("%s: all ROMs found (%zu files searched)\n", set.title, files.size());

    // --- run ---
    std::unique_ptr<Game> m = gi.make();
    m->init(region_ptrs.data(), rate);
    if (dips >= 0) m->set_dips(uint8_t(dips));

    std::vector<std::vector<uint8_t>> sample_bufs;
    std::vector<samples::Wav> wavs;
    if (!samples_path.empty()) {
        if (!gi.sample_count) {
            std::fprintf(stderr, "%s has no sample set\n", gi.name);
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

    std::vector<uint16_t> frame(size_t(gi.width) * gi.height);
    int16_t audio[2048];
    uint16_t held = 0;
    std::vector<std::pair<int, uint16_t>> releases;     // (frame, buttons)
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
        while (next_event < events.size() && events[next_event].frame <= f) {
            const Event& e = events[next_event++];
            if (e.length > 0) {
                held |= e.buttons;
                releases.push_back({e.frame + e.length, e.buttons});
            } else {
                held = e.buttons;
            }
        }
        m->set_inputs(held);

        auto t0 = Clock::now();
        const int n = m->run_frame(audio, int(sizeof audio / sizeof audio[0]));
        const double emu = ms_since(t0);
        wav.write(audio, n);

        t0 = Clock::now();
        for (int y = 0; y < gi.height; ++y) m->render_line(y, frame.data() + y * gi.width);
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
            if (!writers::write_png_rgb565(path, frame.data(), gi.width, gi.height))
                std::fprintf(stderr, "can't write %s\n", path);
        }
    }
    wav.close();
    if (frames > 0)
        std::printf("summary: %d frames (%.1f s of game), emu avg %.3f ms (max %.3f), render avg %.3f ms "
                    "(max %.3f) per %.2f ms frame; watchdog resets %d\n",
                    frames, frames * frame_ms / 1000.0, emu_sum / frames, emu_max, ren_sum / frames,
                    ren_max, frame_ms, m->watchdog_resets());
    return 0;
}
