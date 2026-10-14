// Runs a CP/M .COM program (the ZEXDOC / ZEXALL instruction exercisers) on
// our Z80 core with a tiny BDOS: functions 2 (print char) and 9 (print
// string). Exit code 0 when the program printed no "ERROR".
//
//   zex_test <file.com> [--bench MT]   (--bench: run MT million T-states only
//                                       and print the emulated MHz)
#include "../../emu/cpu/z80.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

uint8_t g_mem[0x10000];
bool    g_quiet = false;
int     g_errors = 0, g_oks = 0;
char    g_line[256];
int     g_line_len = 0;

void put(char c)
{
    if (!g_quiet) { std::putchar(c); if (c == '\n') std::fflush(stdout); }
    if (c == '\n' || c == '\r') {
        g_line[g_line_len] = 0;
        if (std::strstr(g_line, "ERROR")) ++g_errors;
        if (std::strstr(g_line, "OK") || std::strstr(g_line, " ok")) ++g_oks;
        g_line_len = 0;
    } else if (g_line_len < 255) {
        g_line[g_line_len++] = c;
    }
}

// BDOS entry: the stub at FE00 does OUT (0),A; RET.
void bdos_out(void* ctx, uint16_t port, uint8_t)
{
    if ((port & 0xff) != 0) return;
    z80::Cpu& cpu = *static_cast<z80::Cpu*>(ctx);
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
        std::fprintf(stderr, "usage: zex_test <file.com> [--bench MT]\n");
        return 2;
    }
    long bench_mt = 0;
    for (int a = 2; a < argc; ++a)
        if (!std::strcmp(argv[a], "--bench") && a + 1 < argc) bench_mt = std::atol(argv[++a]);

    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "can't open %s\n", argv[1]); return 2; }
    const size_t n = std::fread(g_mem + 0x100, 1, 0x10000 - 0x200, f);
    std::fclose(f);
    if (n == 0) { std::fprintf(stderr, "empty %s\n", argv[1]); return 2; }

    g_mem[0x0000] = 0x76;                                   // warm boot: HALT
    g_mem[0x0005] = 0xc3; g_mem[0x0006] = 0x00; g_mem[0x0007] = 0xfe;   // JP FE00
    g_mem[0xfe00] = 0xd3; g_mem[0xfe01] = 0x00; g_mem[0xfe02] = 0xc9;   // OUT (0),A; RET

    static z80::Cpu cpu;
    cpu.map_ram(0, 0x10000, g_mem);
    cpu.ctx = &cpu;
    cpu.port_out = bdos_out;
    cpu.reset();
    cpu.pc.w = 0x100;
    cpu.sp.w = 0xfe00;

    g_quiet = bench_mt > 0;
    const auto t0 = std::chrono::steady_clock::now();
    unsigned long long total = 0;
    const unsigned long long limit = bench_mt > 0 ? (unsigned long long)bench_mt * 1000000ull : ~0ull;
    while (!(cpu.halted && cpu.pc.w == 1) && total < limit)
        total += (unsigned long long)cpu.run(1000000);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (bench_mt > 0) {
        std::printf("bench: %.0f M T-states in %.2f s = %.1f emulated MHz (%.1fx a 3.072 MHz Z80)\n",
                    total / 1e6, secs, total / 1e6 / secs, total / 1e6 / secs / 3.072);
        return 0;
    }
    std::printf("\n%s: %d ok, %d ERROR, %.0f M T-states, %.1f s (%.1f MHz)\n", argv[1], g_oks, g_errors,
                total / 1e6, secs, total / 1e6 / secs);
    return g_errors ? 1 : 0;
}
