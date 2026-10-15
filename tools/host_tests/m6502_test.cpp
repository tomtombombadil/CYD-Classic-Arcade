// Runs Klaus Dormann's 6502 test programs on our 6502 core (MIT).
//
//   m6502_test functional <6502_functional_test.bin> <success hex> [--bench MC]
//       the 64 KB image at 0000, started at 0400; passes when the program
//       traps (jumps to itself) at the success address
//   m6502_test decimal <image.bin> <load hex>
//       the assembled 6502_decimal_test (tools/host_tests/asm6502.py),
//       started at its load address; ends at its end_of_test byte (DB)
//       and passes when ERROR (zero page 0B) is 0
//   --bench MC: run MC million cycles only and print the emulated MHz
#include "../../emu/cpu/m6502.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

uint8_t g_mem[0x10000];

// The instruction at pc jumps or branches to itself.
bool self_loop(const m6502::Cpu& cpu)
{
    const uint16_t pc = cpu.pc;
    const uint8_t op = g_mem[pc];
    if (op == 0x4c) return (g_mem[uint16_t(pc + 1)] | (g_mem[uint16_t(pc + 2)] << 8)) == pc;
    return (op & 0x1f) == 0x10 && g_mem[uint16_t(pc + 1)] == 0xfe;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: m6502_test functional <bin> <success hex> [--bench MC]\n"
                             "       m6502_test decimal <bin> <load hex>\n");
        return 2;
    }
    const bool decimal = std::strcmp(argv[1], "decimal") == 0;
    const unsigned addr = unsigned(std::strtoul(argv[3], nullptr, 16));
    long bench_mc = 0;
    for (int a = 4; a < argc; ++a)
        if (!std::strcmp(argv[a], "--bench") && a + 1 < argc) bench_mc = std::atol(argv[++a]);

    FILE* f = std::fopen(argv[2], "rb");
    if (!f) { std::fprintf(stderr, "can't open %s\n", argv[2]); return 2; }
    const unsigned load = decimal ? addr : 0;
    const size_t n = std::fread(g_mem + load, 1, sizeof g_mem - load, f);
    std::fclose(f);
    if (n == 0) { std::fprintf(stderr, "empty %s\n", argv[2]); return 2; }

    static m6502::Cpu cpu;
    cpu.map_ram(0, 0x10000, g_mem);
    cpu.reset();
    cpu.pc = uint16_t(decimal ? addr : 0x0400);

    const auto t0 = std::chrono::steady_clock::now();
    unsigned long long total = 0;
    const unsigned long long limit = bench_mc > 0 ? (unsigned long long)bench_mc * 1000000ull : 2000000000ull;
    bool ended = false;
    while (total < limit) {
        if (decimal && g_mem[cpu.pc] == 0xdb) { ended = true; break; }
        if (!decimal && self_loop(cpu)) {
            const uint16_t pc = cpu.pc;
            total += unsigned(cpu.run(1));
            if (cpu.pc == pc) { ended = true; break; }
            continue;
        }
        total += unsigned(cpu.run(decimal ? 1 : 64));
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (bench_mc > 0) {
        std::printf("bench: %.0f M cycles in %.2f s = %.1f emulated MHz (%.0fx a 1.5 MHz 6502)\n", total / 1e6,
                    secs, total / 1e6 / secs, total / 1e6 / secs / 1.5);
        return 0;
    }
    if (!ended) { std::printf("%s: no end after %llu cycles (pc %04x)\n", argv[2], total, cpu.pc); return 1; }
    bool ok;
    if (decimal) {
        ok = g_mem[0x0b] == 0;
        std::printf("6502 decimal test: %s (N1 %02x N2 %02x, carry loop Y %d), %.0f M cycles\n",
                    ok ? "passed" : "FAILED", g_mem[0], g_mem[1], cpu.y, total / 1e6);
    } else {
        ok = cpu.pc == addr;
        std::printf("6502 functional test: %s at %04x, %.0f M cycles, %.2f s, %u undocumented opcodes\n",
                    ok ? "passed" : "TRAPPED", cpu.pc, total / 1e6, secs, unsigned(cpu.illegal_count));
    }
    return ok ? 0 : 1;
}
