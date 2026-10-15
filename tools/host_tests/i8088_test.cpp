// Runs the SingleStepTests 8088 suite (MIT, captured from a real 8088) on
// our 8088 core, plus a few hand-made checks (MIT).
//
//   fetch_i8088_tests.py DIR --cat | i8088_test -   [--verbose] [--cycles]
//   i8088_test --unit                               the hand-made checks only
//   i8088_test --bench MC                           MC million cycles of a loop
//
// Each test sets the registers and RAM, runs one instruction and compares
// every register (flags with the suite's mask of undefined flags) and every
// RAM byte the test lists. The suite's cycle counts include the prefetch
// queue, which the core doesn't model: --cycles prints how far our counts
// are from them per opcode; the run prints the overall ratio.
#include "../../emu/cpu/i8088.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

uint8_t g_mem[1 << 20];

uint8_t mem_read(void*, uint32_t a) { return g_mem[a & 0xfffff]; }
void mem_write(void*, uint32_t a, uint8_t v) { g_mem[a & 0xfffff] = v; }

uint32_t le32(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

// The suite's register order (REGS bitmask bits).
enum { kAx, kBx, kCx, kDx, kCs, kSs, kDs, kEs, kSp, kBp, kSi, kDi, kIp, kFlags, kRegCount };
const char* const kRegNames[kRegCount] = {"ax", "bx", "cx", "dx", "cs", "ss", "ds", "es",
                                          "sp", "bp", "si", "di", "ip", "flags"};

struct State {
    uint16_t mask = 0;
    uint16_t regs[kRegCount] = {};
    std::vector<std::pair<uint32_t, uint8_t>> ram;
};

struct Test {
    std::string name;
    State init, fina;
    uint32_t cycles = 0;
};

// Reads a REGS / RAM / QUEU state chunk payload.
void read_state(const uint8_t* p, uint32_t len, State& st)
{
    uint32_t pos = 0;
    while (pos + 8 <= len) {
        const uint8_t* c = p + pos;
        const uint32_t n = le32(c + 4);
        if (pos + 8 + n > len) break;
        const uint8_t* d = c + 8;
        if (!std::memcmp(c, "REGS", 4)) {
            st.mask = le16(d);
            int k = 0;
            for (int r = 0; r < kRegCount; ++r)
                if (st.mask >> r & 1) st.regs[r] = le16(d + 2 + 2 * k++);
        } else if (!std::memcmp(c, "RAM ", 4)) {
            const uint32_t cnt = le32(d);
            for (uint32_t i = 0; i < cnt; ++i) st.ram.push_back({le32(d + 4 + 5 * i), d[8 + 5 * i]});
        }
        pos += 8 + n;
    }
}

bool parse_test(const uint8_t* p, uint32_t len, Test& t)
{
    uint32_t pos = 4;   // the index
    while (pos + 8 <= len) {
        const uint8_t* c = p + pos;
        const uint32_t n = le32(c + 4);
        if (pos + 8 + n > len) return false;
        const uint8_t* d = c + 8;
        if (!std::memcmp(c, "NAME", 4)) t.name.assign(reinterpret_cast<const char*>(d + 4), le32(d));
        else if (!std::memcmp(c, "INIT", 4)) read_state(d, n, t.init);
        else if (!std::memcmp(c, "FINA", 4)) read_state(d, n, t.fina);
        else if (!std::memcmp(c, "CYCL", 4)) t.cycles = le32(d);
        pos += 8 + n;
    }
    return true;
}

void set_reg(i8088::Cpu& cpu, int r, uint16_t v)
{
    switch (r) {
    case kAx: cpu.r[i8088::AX] = v; break;
    case kBx: cpu.r[i8088::BX] = v; break;
    case kCx: cpu.r[i8088::CX] = v; break;
    case kDx: cpu.r[i8088::DX] = v; break;
    case kCs: cpu.sreg[i8088::CS] = v; break;
    case kSs: cpu.sreg[i8088::SS] = v; break;
    case kDs: cpu.sreg[i8088::DS] = v; break;
    case kEs: cpu.sreg[i8088::ES] = v; break;
    case kSp: cpu.r[i8088::SP] = v; break;
    case kBp: cpu.r[i8088::BP] = v; break;
    case kSi: cpu.r[i8088::SI] = v; break;
    case kDi: cpu.r[i8088::DI] = v; break;
    case kIp: cpu.ip = v; break;
    default: cpu.set_flags(v); break;
    }
}

uint16_t get_reg(const i8088::Cpu& cpu, int r)
{
    switch (r) {
    case kAx: return cpu.r[i8088::AX];
    case kBx: return cpu.r[i8088::BX];
    case kCx: return cpu.r[i8088::CX];
    case kDx: return cpu.r[i8088::DX];
    case kCs: return cpu.sreg[i8088::CS];
    case kSs: return cpu.sreg[i8088::SS];
    case kDs: return cpu.sreg[i8088::DS];
    case kEs: return cpu.sreg[i8088::ES];
    case kSp: return cpu.r[i8088::SP];
    case kBp: return cpu.r[i8088::BP];
    case kSi: return cpu.r[i8088::SI];
    case kDi: return cpu.r[i8088::DI];
    case kIp: return cpu.ip;
    default: return cpu.flags();
    }
}

// Runs one test; returns an empty string when it passes, else what differs.
std::string run_test(const Test& t, uint16_t flag_mask, int& our_cycles)
{
    static i8088::Cpu cpu;
    cpu.ctx = nullptr;
    cpu.mem_read = mem_read;
    cpu.mem_write = mem_write;
    for (const auto& m : t.init.ram) g_mem[m.first & 0xfffff] = m.second;
    for (int r = 0; r < kRegCount; ++r) set_reg(cpu, r, t.init.regs[r]);
    our_cycles = cpu.step_instruction();

    std::string why;
    char buf[160];
    for (int r = 0; r < kRegCount; ++r) {
        uint16_t want = (t.fina.mask >> r & 1) ? t.fina.regs[r] : t.init.regs[r];
        uint16_t got = get_reg(cpu, r);
        if (r == kFlags) {
            // The suite's flags have the 8088's fixed bits; compare the real ones.
            want &= flag_mask & 0x0fd5;
            got &= flag_mask & 0x0fd5;
        }
        if (want != got) {
            std::snprintf(buf, sizeof buf, " %s=%04x (want %04x)", kRegNames[r], got, want);
            why += buf;
        }
    }
    // A divide error pushes the flags, undefined ones included: compare the
    // pushed word (SS:SP+4 after the interrupt) through the mask too.
    const uint32_t flags_at = cpu.phys(cpu.sreg[i8088::SS], uint16_t(cpu.r[i8088::SP] + 4)) & 0xfffff;
    const bool trapped = cpu.ip == (g_mem[0] | g_mem[1] << 8) && cpu.sreg[i8088::CS] == (g_mem[2] | g_mem[3] << 8);
    for (const auto& m : t.fina.ram) {
        uint8_t got = g_mem[m.first & 0xfffff], want = m.second;
        if (trapped && (m.first == flags_at || m.first == flags_at + 1)) {
            const uint8_t mk = uint8_t((flag_mask & 0x0fd5) >> (m.first == flags_at ? 0 : 8));
            got &= mk;
            want &= mk;
        }
        if (got != want) {
            std::snprintf(buf, sizeof buf, " [%05x]=%02x (want %02x)", m.first, got, want);
            why += buf;
        }
    }
    // Leave memory as it was found for the next test (the suite's states are
    // sparse; stale bytes elsewhere don't matter).
    for (const auto& m : t.fina.ram) g_mem[m.first & 0xfffff] = 0;
    for (const auto& m : t.init.ram) g_mem[m.first & 0xfffff] = 0;
    return why;
}

bool read_exact(FILE* f, void* p, size_t n) { return std::fread(p, 1, n, f) == n; }

int run_suite(bool verbose, bool show_cycles)
{
    char line[256];
    int files = 0;
    long total = 0, failed = 0;
    double ratio_sum = 0;
    long ratio_n = 0;
    std::vector<uint8_t> data;
    while (std::fgets(line, sizeof line, stdin)) {
        char name[32];
        unsigned mask = 0;
        unsigned long len = 0;
        if (std::sscanf(line, "FILE %31s %x %lu", name, &mask, &len) != 3) {
            std::fprintf(stderr, "i8088_test: bad stream header: %s", line);
            return 2;
        }
        data.resize(len);
        if (!read_exact(stdin, data.data(), len)) {
            std::fprintf(stderr, "i8088_test: %s: short data\n", name);
            return 2;
        }
        ++files;
        long file_tests = 0, file_failed = 0;
        double file_ours = 0, file_theirs = 0;
        uint32_t pos = 0;
        while (pos + 8 <= len) {
            const uint8_t* c = data.data() + pos;
            const uint32_t n = le32(c + 4);
            if (pos + 8 + n > len) break;          // the cut-off last test
            if (!std::memcmp(c, "TEST", 4)) {
                Test t;
                if (parse_test(c + 8, n, t)) {
                    int ours = 0;
                    const std::string why = run_test(t, uint16_t(mask), ours);
                    ++file_tests;
                    file_ours += ours;
                    file_theirs += t.cycles;
                    if (!why.empty()) {
                        if (file_failed < 3 || verbose)
                            std::printf("FAIL %s #%ld \"%s\":%s\n", name, file_tests - 1, t.name.c_str(),
                                        why.c_str());
                        ++file_failed;
                    }
                }
            }
            pos += 8 + n;
        }
        total += file_tests;
        failed += file_failed;
        if (file_failed) std::printf("%s: %ld of %ld failed\n", name, file_failed, file_tests);
        if (file_theirs > 0) {
            ratio_sum += file_ours / file_theirs;
            ++ratio_n;
        }
        if (show_cycles && file_tests)
            std::printf("cycles %-5s ours %6.1f  8088 %6.1f  (%.2f)\n", name, file_ours / file_tests,
                        file_theirs / file_tests, file_theirs > 0 ? file_ours / file_theirs : 0.0);
    }
    if (files == 0) {
        std::fprintf(stderr, "i8088_test: no tests read\n");
        return 2;
    }
    std::printf("8088 suite: %ld tests in %d files, %ld failed; cycles ours / 8088 (with its queue) "
                "%.2f on average\n", total, files, failed, ratio_n ? ratio_sum / ratio_n : 0.0);
    return failed ? 1 : 0;
}

// --- hand-made checks (no downloads) -----------------------------------------

int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

void load(i8088::Cpu&, uint32_t at, std::initializer_list<uint8_t> code)
{
    uint32_t a = at;
    for (uint8_t b : code) g_mem[a++] = b;
}

int unit()
{
    std::memset(g_mem, 0, sizeof g_mem);
    i8088::Cpu cpu;
    cpu.mem_read = mem_read;
    cpu.mem_write = mem_write;

    // Reset: FFFF:0000, a far jump to 0000:0100.
    load(cpu, 0xffff0, {0xea, 0x00, 0x01, 0x00, 0x00});
    cpu.reset();
    CHECK(cpu.sreg[i8088::CS] == 0xffff && cpu.ip == 0);
    cpu.step_instruction();
    CHECK(cpu.sreg[i8088::CS] == 0 && cpu.ip == 0x100);

    // A loop: MOV CX,10; MOV AL,0; ADD AL,3; LOOP -4; HLT.
    load(cpu, 0x100, {0xb9, 0x0a, 0x00, 0xb0, 0x00, 0x04, 0x03, 0xe2, 0xfc, 0xf4, 0xf4});
    const int c = cpu.run(10000);
    CHECK(cpu.halted());
    CHECK((cpu.r[i8088::AX] & 0xff) == 30 && cpu.r[i8088::CX] == 0);
    CHECK(c == 10000);      // a halted CPU spends the budget

    // NMI wakes HLT; the handler (vector 2) runs, IRET returns past the HLT
    // to the next one.
    g_mem[8] = 0x00; g_mem[9] = 0x02; g_mem[10] = 0; g_mem[11] = 0;    // 0000:0200
    load(cpu, 0x200, {0x40, 0xcf});                                       // INC AX; IRET
    cpu.r[i8088::SP] = 0x1000;
    cpu.nmi();
    cpu.run(200);
    CHECK(cpu.halted());
    CHECK((cpu.r[i8088::AX] & 0xff) == 31);
    CHECK(cpu.ip == 0x10b && cpu.r[i8088::SP] == 0x1000);

    // A word read at offset FFFF wraps within the segment.
    g_mem[0x1ffff] = 0x34;
    g_mem[0x10000] = 0x12;
    load(cpu, 0x300, {0xa1, 0xff, 0xff});       // MOV AX,[FFFF] with DS = 1000
    cpu.sreg[i8088::DS] = 0x1000;
    cpu.ip = 0x300;
    cpu.step_instruction();
    CHECK(cpu.r[i8088::AX] == 0x1234);

    // REP STOSB stops between elements when the budget ends and resumes.
    load(cpu, 0x400, {0xf3, 0xaa, 0xf4});       // REP STOSB; HLT
    cpu.sreg[i8088::ES] = 0x2000;
    cpu.r[i8088::DI] = 0;
    cpu.r[i8088::CX] = 100;
    cpu.r[i8088::AX] = 0x55;
    cpu.set_flags(0);
    cpu.ip = 0x400;
    cpu.run(50);
    CHECK(cpu.r[i8088::CX] > 0 && cpu.r[i8088::CX] < 100 && cpu.ip == 0x400);
    cpu.run(5000);
    CHECK(cpu.r[i8088::CX] == 0 && g_mem[0x20000] == 0x55 && g_mem[0x20063] == 0x55 && g_mem[0x20064] == 0);
    CHECK(cpu.halted());

    // The low 64 KB through the page table, with a 16-bit address mask.
    static uint8_t page[256];
    i8088::Cpu b;
    b.addr_mask = 0xffff;
    b.map_ram(0x4000, 256, page);
    b.mem_read = mem_read;
    b.mem_write = mem_write;
    b.wr8(0x14010, 0x77);           // A16-A19 dropped: 0x4010
    CHECK(page[0x10] == 0x77 && b.rd8(0x4010) == 0x77);

    std::printf("8088 unit checks: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}

// A small benchmark: a loop of typical instructions in RAM.
int bench(long mc)
{
    std::memset(g_mem, 0, sizeof g_mem);
    static uint8_t ram[0x10000];
    i8088::Cpu cpu;
    cpu.addr_mask = 0xffff;
    cpu.map_ram(0, 0x10000, ram);
    const uint8_t code[] = {
        0xbe, 0x00, 0x20,       // MOV SI,2000
        0xb9, 0x40, 0x00,       // MOV CX,64
        0x8a, 0x04,             // MOV AL,[SI]
        0x02, 0x44, 0x01,       // ADD AL,[SI+1]
        0x88, 0x44, 0x02,       // MOV [SI+2],AL
        0x46,                   // INC SI
        0x3c, 0x80,             // CMP AL,80
        0x72, 0x02,             // JB +2
        0x34, 0x55,             // XOR AL,55
        0xe2, 0xf1,             // LOOP back to MOV AL,[SI]
        0xeb, 0xe9,             // JMP to start
    };
    std::memcpy(ram + 0x100, code, sizeof code);
    cpu.sreg[i8088::CS] = 0;
    cpu.ip = 0x100;
    const auto t0 = std::chrono::steady_clock::now();
    long long done = 0;
    while (done < mc * 1000000LL) done += cpu.run(100000);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("8088 benchmark: %.0f emulated MHz, %.1f M instructions/s\n", done / s / 1e6,
                double(cpu.instructions) / s / 1e6);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    bool verbose = false, cycles = false;
    for (int a = 1; a < argc; ++a) {
        if (!std::strcmp(argv[a], "--unit")) return unit();
        if (!std::strcmp(argv[a], "--bench") && a + 1 < argc) return bench(std::atol(argv[a + 1]));
        if (!std::strcmp(argv[a], "--verbose")) verbose = true;
        if (!std::strcmp(argv[a], "--cycles")) cycles = true;
    }
    if (argc < 2 || std::strcmp(argv[1], "-") != 0) {
        std::fprintf(stderr, "usage: fetch_i8088_tests.py DIR --cat | i8088_test - [--verbose] [--cycles]\n"
                             "       i8088_test --unit | --bench MC\n");
        return 2;
    }
    const int u = unit();
    return run_suite(verbose, cycles) | u;
}
