// Runs a CP/M .COM program (the 8080 CPU tests: 8080PRE, 8080EXM, TST8080,
// CPUTEST) on our 8080 core with a tiny BDOS: functions 2 (print char) and
// 9 (print string). Exit code 0 when the program printed no "ERROR" /
// "FAILED" and, with --expect, printed that text.
//
//   i8080_test <file.com> [--expect TEXT] [--bench MS]
//     --bench: run MS million states only and print the emulated MHz
#include "../../emu/cpu/i8080.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

uint8_t     g_mem[0x10000];
bool        g_quiet = false;
int         g_errors = 0;
std::string g_all;              // everything printed (for --expect)
char        g_line[256];
int         g_line_len = 0;

void put(char c)
{
    if (!g_quiet) { std::putchar(c); if (c == '\n') std::fflush(stdout); }
    g_all.push_back(c);
    if (c == '\n' || c == '\r') {
        g_line[g_line_len] = 0;
        if (std::strstr(g_line, "ERROR") || std::strstr(g_line, "FAILED")) ++g_errors;
        g_line_len = 0;
    } else if (g_line_len < 255) {
        g_line[g_line_len++] = c;
    }
}

// BDOS entry: the stub at FE00 does OUT 0; RET.
void bdos_out(void* ctx, uint16_t port, uint8_t)
{
    if ((port & 0xff) != 0) return;
    i8080::Cpu& cpu = *static_cast<i8080::Cpu*>(ctx);
    switch (cpu.bc.b.l) {
    case 2:
        put(char(cpu.de.b.l));
        break;
    case 9:
        for (uint16_t a = cpu.de.w; g_mem[a] != '$'; ++a) put(char(g_mem[a]));
        break;
    default:
        break;
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: i8080_test <file.com> [--expect TEXT] [--bench MS]\n");
        return 2;
    }
    long bench_ms = 0;
    const char* expect = nullptr;
    for (int a = 2; a < argc; ++a) {
        if (!std::strcmp(argv[a], "--bench") && a + 1 < argc) bench_ms = std::atol(argv[++a]);
        else if (!std::strcmp(argv[a], "--expect") && a + 1 < argc) expect = argv[++a];
    }

    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "can't open %s\n", argv[1]); return 2; }
    const size_t n = std::fread(g_mem + 0x100, 1, 0x10000 - 0x200, f);
    std::fclose(f);
    if (n == 0) { std::fprintf(stderr, "empty %s\n", argv[1]); return 2; }

    g_mem[0x0000] = 0x76;                                   // warm boot: HLT
    g_mem[0x0005] = 0xc3; g_mem[0x0006] = 0x00; g_mem[0x0007] = 0xfe;   // JMP FE00
    g_mem[0xfe00] = 0xd3; g_mem[0xfe01] = 0x00; g_mem[0xfe02] = 0xc9;   // OUT 0; RET
    // Programs that size memory read the BDOS address at 0006: FE00 leaves
    // room for their stacks below it.

    static i8080::Cpu cpu;
    cpu.map_ram(0, 0x10000, g_mem);
    cpu.ctx = &cpu;
    cpu.port_out = bdos_out;
    cpu.reset();
    cpu.pc.w = 0x100;
    cpu.sp.w = 0xfe00;

    g_quiet = bench_ms > 0;
    const auto t0 = std::chrono::steady_clock::now();
    unsigned long long total = 0;
    const unsigned long long limit = bench_ms > 0 ? (unsigned long long)bench_ms * 1000000ull : ~0ull;
    while (!(cpu.halted && cpu.pc.w == 1) && total < limit)
        total += (unsigned long long)cpu.run(100000);     // small slices: the final HLT burns little
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (bench_ms > 0) {
        std::printf("bench: %.0f M states in %.2f s = %.1f emulated MHz (%.1fx a 1.9968 MHz 8080)\n",
                    total / 1e6, secs, total / 1e6 / secs, total / 1e6 / secs / 1.9968);
        return 0;
    }
    const bool found = !expect || g_all.find(expect) != std::string::npos;
    std::printf("\n%s: %d ERROR, %s, %.1f M states, %.1f s\n", argv[1], g_errors,
                expect ? (found ? "expected text found" : "expected text MISSING") : "no text expected",
                total / 1e6, secs);
    return g_errors || !found ? 1 : 0;
}
