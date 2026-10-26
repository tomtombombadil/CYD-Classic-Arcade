// xsim: a small Xtensa LX7 (ESP32-S3) instruction-set simulator with a
// cycle estimate, to see what the emulators cost on the board (Claude's
// side; build with tools/xtensa_sim/build.sh, which says how to run it).
//
//   xsim game.elf DUMPDIR GAME FRAMES [options]
//     DUMPDIR: r0.bin ... (ROM regions), inputs.bin (arcade_host --dump)
//   --hash FILE     per frame: picture CRC, audio CRC, samples (as arcade_host
//                   --hash: the same CRCs - checks the simulator, and the
//                   compiled code, against the PC)
//   --prof FILE     cycles / instructions / cache misses a frame per function
//                   (FILE.pc: per instruction address)
//   --render        draw each frame (implied by --hash; not counted)
//   --skip N        don't count the first N frames
//   --demo on|off
//   --param k=v     the model: br, ovf, icmiss, dflash, dpsram, ld, mul, div,
//                   fp, icache_kb, ways, dcache_kb
//
// What it runs: the core ISA GCC emits for the S3 (narrow forms, windowed
// calls with the register file's overflow / underflow counted, zero-overhead
// loops, MUL / DIV, booleans, the single-precision FPU with fused MADD.S);
// __divsf3 is done natively (the FPU's division sequence isn't simulated).
// A program that runs exactly gives the PC's pictures (checked: Gyruss 1500 -
// 3000 frames, every picture and, built with -ffp-contract=off, every
// sample the same as arcade_host's).
//
// The cycle model (rough): one instruction a cycle; a register read before
// its value is ready stalls (loads 2, mul 2, div 12, float 4); a taken
// branch, jump, call or return +2; a window overflow / underflow 20;
// instruction cache (flash text and L32R literals) 16 KB, 8-way, 32-byte
// lines, a miss 200 cycles; data cache 32 KB 8-way for flash .rodata and the
// ROM regions in PSRAM; internal RAM uncached. The board's other core (BLE,
// drawing, the LCD) shares the caches and isn't simulated, so absolute
// times are low: fitted to Tom's S3 pages (Gyruss v0.23.0 / v0.24.1,
// Elevator Action, Donkey Kong Jr., 1942, 2026-10-24) the board took ~1.2-1.35
// cycles an instruction outside misses plus ~1,670 cycles a simulated miss
// at 16 KB (or ~470 at 8 KB, ~155 at 4 KB: the fits are about as good, ~10 %
// rms) - use the counts to compare versions, not as milliseconds.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

namespace {

// ---- parameters ----
int P_BR = 2, P_OVF = 20, P_ICMISS = 200, P_DFLASH = 200, P_DPSRAM = 120, P_LD = 2, P_MUL = 2, P_DIV = 12, P_FP = 4;
int P_ICACHE_KB = 16, P_DCACHE_KB = 32, P_FDIV = 40;
uint32_t g_divsf3 = 0;

// ---- memory ----
enum Kind : uint8_t { K_NONE, K_DRAM, K_FLASHD, K_PSRAM, K_FLASHI, K_HOST };
struct Page { uint8_t* p; Kind k; };
Page g_pages[65536];

constexpr uint32_t kIrom = 0x42000000, kIromSize = 4u << 20;
constexpr uint32_t kDrom = 0x3C000000, kDromSize = 8u << 20;
constexpr uint32_t kPsram = 0x3D000000, kPsramSize = 8u << 20;
constexpr uint32_t kDram = 0x3FC80000, kDramSize = 512u << 10;
constexpr uint32_t kHost = 0x50000000, kHostSize = 16u << 20;
constexpr uint32_t kDoor = 0x50001020;

std::vector<uint8_t> g_irom(kIromSize), g_drom(kDromSize), g_psram(kPsramSize), g_dram(kDramSize), g_host(kHostSize);

void map(uint32_t base, std::vector<uint8_t>& v, Kind k)
{
    for (uint32_t off = 0; off < v.size(); off += 0x10000) g_pages[(base + off) >> 16] = {v.data() + off, k};
}

[[noreturn]] void die(const char* msg, uint32_t a = 0, uint32_t pc = 0)
{
    std::fprintf(stderr, "xsim: %s %08x (pc %08x)\n", msg, a, pc);
    std::exit(3);
}

// ---- caches ----
struct Cache {
    int sets = 0, ways = 8, line_shift = 5;
    std::vector<uint32_t> tag;
    std::vector<uint32_t> age;
    uint32_t clock = 0;
    uint64_t hits = 0, misses = 0;
    void init(int kb) {
        sets = kb * 1024 / 32 / ways;
        tag.assign(size_t(sets) * ways, 0xffffffffu);
        age.assign(size_t(sets) * ways, 0);
    }
    // true = hit
    bool access(uint32_t addr) {
        const uint32_t line = addr >> line_shift;
        const int set = int(line & uint32_t(sets - 1));
        uint32_t* t = &tag[size_t(set) * ways];
        uint32_t* a = &age[size_t(set) * ways];
        ++clock;
        for (int w = 0; w < ways; ++w)
            if (t[w] == line) { a[w] = clock; ++hits; return true; }
        int v = 0;
        for (int w = 1; w < ways; ++w) if (a[w] < a[v]) v = w;
        t[v] = line;
        a[v] = clock;
        ++misses;
        return false;
    }
};
Cache g_ic, g_dc;

// ---- CPU ----
uint32_t g_ar[1 << 18];
uint64_t g_rdy[1 << 18];
float    g_f[16];
uint64_t g_frdy[16];
uint32_t g_br = 0;          // boolean registers b0-b15
uint32_t g_sar = 0, g_lbeg = 0, g_lend = 0, g_lcount = 0, g_ps = 0;
int      g_wb = 4;          // window base (virtual, in quads)
uint32_t g_ws = 0;          // physical WindowStart (16 bits)
int      g_callinc = 0;
uint32_t g_pc = 0;
uint64_t g_cyc = 0;
uint64_t g_ovf = 0, g_unf = 0;
bool     g_count = false;
uint64_t g_extra = 0;       // stall cycles added by memory in this instruction
uint32_t g_last_iline = 0xffffffffu;

inline uint32_t* A() { return g_ar + g_wb * 4; }

inline uint8_t* host_ptr(uint32_t a, Kind& k)
{
    const Page& p = g_pages[a >> 16];
    k = p.k;
    if (!p.p) die("bad address", a, g_pc);
    return p.p + (a & 0xffff);
}

inline void dcache(uint32_t a, Kind k)
{
    if (k == K_FLASHD) { if (!g_dc.access(a)) g_extra += P_DFLASH; }
    else if (k == K_PSRAM) { if (!g_dc.access(a)) g_extra += P_DPSRAM; }
    else if (k == K_FLASHI) { if (!g_ic.access(a)) g_extra += P_ICMISS; }
}

inline uint32_t ld32(uint32_t a) { Kind k; uint8_t* p = host_ptr(a & ~3u, k); dcache(a, k); uint32_t v; std::memcpy(&v, p, 4); return v; }
inline uint32_t ld16(uint32_t a) { Kind k; uint8_t* p = host_ptr(a & ~1u, k); dcache(a, k); uint16_t v; std::memcpy(&v, p, 2); return v; }
inline uint32_t ld8(uint32_t a)  { Kind k; uint8_t* p = host_ptr(a, k); dcache(a, k); return *p; }

void door(uint32_t cmd);

inline void st32(uint32_t a, uint32_t v)
{
    Kind k; uint8_t* p = host_ptr(a & ~3u, k);
    if (k == K_FLASHD || k == K_FLASHI) die("store to flash", a, g_pc);
    if (k == K_PSRAM) dcache(a, k);
    std::memcpy(p, &v, 4);
    if (k == K_HOST && (a & ~3u) == kDoor) door(v);
}
inline void st16(uint32_t a, uint32_t v) { Kind k; uint8_t* p = host_ptr(a & ~1u, k); if (k == K_FLASHD || k == K_FLASHI) die("store16 to flash", a, g_pc); uint16_t x = uint16_t(v); std::memcpy(p, &x, 2); }
inline void st8(uint32_t a, uint32_t v)  { Kind k; uint8_t* p = host_ptr(a, k); if (k == K_FLASHD || k == K_FLASHI) die("store8 to flash", a, g_pc); *p = uint8_t(v); }

inline float fbits(uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; }
inline uint32_t bitsf(float f) { uint32_t v; std::memcpy(&v, &f, 4); return v; }

// ---- decoding ----
enum Op : uint8_t {
    O_ILL, O_NOP,
    O_ADD, O_ADDX2, O_ADDX4, O_ADDX8, O_SUB, O_SUBX2, O_SUBX4, O_SUBX8, O_AND, O_OR, O_XOR,
    O_SSR, O_SSL, O_SSA8L, O_SSA8B, O_SSAI, O_NSA, O_NSAU, O_NEG, O_ABS,
    O_SLLI, O_SRAI, O_SRLI, O_SRC, O_SRL, O_SLL, O_SRA, O_MUL16U, O_MUL16S,
    O_SALTU, O_SALT, O_MULL, O_MULUH, O_MULSH, O_QUOU, O_QUOS, O_REMU, O_REMS,
    O_ANDB, O_ANDBC, O_ORB, O_ORBC, O_XORB, O_ANY4, O_ALL4, O_ANY8, O_ALL8,
    O_RSR, O_WSR, O_XSR, O_SEXT, O_CLAMPS, O_MIN, O_MAX, O_MINU, O_MAXU,
    O_MOVEQZ, O_MOVNEZ, O_MOVLTZ, O_MOVGEZ, O_MOVF, O_MOVT, O_RUR, O_WUR,
    O_EXTUI,
    O_RET, O_RETW, O_JX, O_CALLX0, O_CALLX4, O_CALLX8, O_CALLX12, O_MOVSP, O_BREAK, O_SIMCALL, O_RSIL,
    O_L32R, O_L8UI, O_L16UI, O_L16SI, O_L32I, O_S8I, O_S16I, O_S32I, O_MOVI, O_ADDI, O_ADDMI,
    O_LSI, O_SSI, O_LSIU, O_SSIU, O_LSX, O_LSXU, O_SSX, O_SSXU,
    O_CALL0, O_CALL4, O_CALL8, O_CALL12, O_J,
    O_BEQZ, O_BNEZ, O_BLTZ, O_BGEZ, O_BEQI, O_BNEI, O_BLTI, O_BGEI, O_BLTUI, O_BGEUI,
    O_ENTRY, O_BF, O_BT, O_LOOP, O_LOOPNEZ, O_LOOPGTZ,
    O_BNONE, O_BEQ, O_BLT, O_BLTU, O_BALL, O_BBC, O_BBCI, O_BANY, O_BNE, O_BGE, O_BGEU, O_BNALL, O_BBS, O_BBSI,
    O_ADDN, O_ADDIN, O_MOVIN, O_BEQZN, O_BNEZN, O_MOVN, O_RETN, O_RETWN,
    O_ADDS, O_SUBS, O_MULS, O_MADDS, O_MSUBS, O_ROUNDS, O_TRUNCS, O_FLOORS, O_CEILS, O_FLOATS, O_UFLOATS, O_UTRUNCS,
    O_MOVS, O_ABSS, O_RFR, O_WFR, O_NEGS,
    O_UNS, O_OEQS, O_UEQS, O_OLTS, O_ULTS, O_OLES, O_ULES,
    O_MOVEQZS, O_MOVNEZS, O_MOVLTZS, O_MOVGEZS, O_MOVFS, O_MOVTS,
    O_COUNT
};

struct Dec {
    uint8_t op = 0, len = 0, r = 0, s = 0, t = 0, maxq = 0, decoded = 0, pad = 0;
    int32_t imm = 0;
    uint32_t target = 0;
};
std::vector<Dec> g_dec;     // per irom byte

constexpr int32_t kB4[16] = {-1, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32, 64, 128, 256};
constexpr uint32_t kB4u[16] = {32768, 65536, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32, 64, 128, 256};

inline int32_t sext(uint32_t v, int bits) { return int32_t(v << (32 - bits)) >> (32 - bits); }

void decode(uint32_t pc, Dec& d)
{
    const uint8_t* p = &g_irom[pc - kIrom];
    const uint32_t w = p[0] | (p[1] << 8) | (uint32_t(p[2]) << 16);
    const uint32_t op0 = w & 15;
    d.decoded = 1;
    d.op = O_ILL;
    if (op0 >= 8 && op0 <= 13) {
        d.len = 2;
        const uint8_t t = (w >> 4) & 15, s = (w >> 8) & 15, r = (w >> 12) & 15;
        d.r = r; d.s = s; d.t = t;
        int mx = 0;
        switch (op0) {
        case 8: d.op = O_L32I; d.imm = r * 4; mx = std::max(s, t); break;
        case 9: d.op = O_S32I; d.imm = r * 4; mx = std::max(s, t); break;
        case 10: d.op = O_ADDN; mx = std::max({r, s, t}); break;
        case 11: d.op = O_ADDIN; d.imm = t ? t : -1; mx = std::max(r, s); break;
        case 12:
            if (!(t & 8)) {
                int v = ((t & 7) << 4) | r;
                if ((v & 0x60) == 0x60) v -= 128;
                d.op = O_MOVIN; d.imm = v; mx = s;
            } else {
                d.op = (t & 4) ? O_BNEZN : O_BEQZN;
                d.target = pc + 4 + (((t & 3) << 4) | r);
                mx = s;
            }
            break;
        case 13:
            if (r == 0) { d.op = O_MOVN; mx = std::max(s, t); }
            else if (r == 15) {
                if (t == 0) d.op = O_RETN;
                else if (t == 1) d.op = O_RETWN;
                else if (t == 3) d.op = O_NOP;
                else if (t == 2) d.op = O_BREAK;
            }
            break;
        }
        d.maxq = uint8_t(mx >> 2);
        return;
    }
    d.len = 3;
    const uint8_t t = (w >> 4) & 15, s = (w >> 8) & 15, r = (w >> 12) & 15;
    const uint32_t op1 = (w >> 16) & 15, op2 = (w >> 20) & 15;
    const uint32_t imm8 = (w >> 16) & 255;
    d.r = r; d.s = s; d.t = t;
    int mx = std::max({r, s, t});
    switch (op0) {
    case 0:                                             // QRST
        switch (op1) {
        case 0:                                         // RST0
            switch (op2) {
            case 0:                                     // ST0
                if (r == 0) {
                    const int m = t >> 2, n = t & 3;
                    mx = s;
                    if (m == 2) { d.op = n == 0 ? O_RET : n == 1 ? O_RETW : n == 2 ? O_JX : O_ILL; if (n < 2) mx = 0; }
                    else if (m == 3) { d.op = uint8_t(O_CALLX0 + n); mx = std::max(int(s), n * 4); }
                } else if (r == 1) d.op = O_MOVSP;
                else if (r == 2) d.op = O_NOP;          // syncs, memw, excw, nop
                else if (r == 4) d.op = O_BREAK;
                else if (r == 5) { d.op = s == 1 ? O_SIMCALL : O_BREAK; mx = 0; }
                else if (r == 6) { d.op = O_RSIL; mx = t; }
                else if (r == 8) d.op = O_ANY4;
                else if (r == 9) d.op = O_ALL4;
                else if (r == 10) d.op = O_ANY8;
                else if (r == 11) d.op = O_ALL8;
                if (r >= 8) mx = 0;
                break;
            case 1: d.op = O_AND; break;
            case 2: d.op = O_OR; break;
            case 3: d.op = O_XOR; break;
            case 4:
                mx = s;
                switch (r) {
                case 0: d.op = O_SSR; break;
                case 1: d.op = O_SSL; break;
                case 2: d.op = O_SSA8L; break;
                case 3: d.op = O_SSA8B; break;
                case 4: d.op = O_SSAI; d.imm = s | ((t & 1) << 4); mx = 0; break;
                case 14: d.op = O_NSA; mx = std::max(s, t); break;
                case 15: d.op = O_NSAU; mx = std::max(s, t); break;
                }
                break;
            case 6: d.op = s == 0 ? O_NEG : s == 1 ? O_ABS : O_ILL; mx = std::max(r, t); break;
            case 8: d.op = O_ADD; break;
            case 9: d.op = O_ADDX2; break;
            case 10: d.op = O_ADDX4; break;
            case 11: d.op = O_ADDX8; break;
            case 12: d.op = O_SUB; break;
            case 13: d.op = O_SUBX2; break;
            case 14: d.op = O_SUBX4; break;
            case 15: d.op = O_SUBX8; break;
            }
            break;
        case 1:                                         // RST1
            switch (op2) {
            case 0: case 1: d.op = O_SLLI; d.imm = 32 - int(((op2 & 1) << 4) | t); mx = std::max(r, s); break;
            case 2: case 3: d.op = O_SRAI; d.imm = int(((op2 & 1) << 4) | s); mx = std::max(r, t); break;
            case 4: d.op = O_SRLI; d.imm = s; mx = std::max(r, t); break;
            case 6: d.op = O_XSR; d.imm = (r << 4) | s; mx = t; break;
            case 8: d.op = O_SRC; break;
            case 9: d.op = O_SRL; mx = std::max(r, t); break;
            case 10: d.op = O_SLL; mx = std::max(r, s); break;
            case 11: d.op = O_SRA; mx = std::max(r, t); break;
            case 12: d.op = O_MUL16U; break;
            case 13: d.op = O_MUL16S; break;
            }
            break;
        case 2:                                         // RST2
            switch (op2) {
            case 0: d.op = O_ANDB; mx = 0; break;
            case 1: d.op = O_ANDBC; mx = 0; break;
            case 2: d.op = O_ORB; mx = 0; break;
            case 3: d.op = O_ORBC; mx = 0; break;
            case 4: d.op = O_XORB; mx = 0; break;
            case 6: d.op = O_SALTU; break;
            case 7: d.op = O_SALT; break;
            case 8: d.op = O_MULL; break;
            case 10: d.op = O_MULUH; break;
            case 11: d.op = O_MULSH; break;
            case 12: d.op = O_QUOU; break;
            case 13: d.op = O_QUOS; break;
            case 14: d.op = O_REMU; break;
            case 15: d.op = O_REMS; break;
            }
            break;
        case 3:                                         // RST3
            switch (op2) {
            case 0: d.op = O_RSR; d.imm = (r << 4) | s; mx = t; break;
            case 1: d.op = O_WSR; d.imm = (r << 4) | s; mx = t; break;
            case 2: d.op = O_SEXT; mx = std::max(r, s); break;
            case 3: d.op = O_CLAMPS; mx = std::max(r, s); break;
            case 4: d.op = O_MIN; break;
            case 5: d.op = O_MAX; break;
            case 6: d.op = O_MINU; break;
            case 7: d.op = O_MAXU; break;
            case 8: d.op = O_MOVEQZ; break;
            case 9: d.op = O_MOVNEZ; break;
            case 10: d.op = O_MOVLTZ; break;
            case 11: d.op = O_MOVGEZ; break;
            case 12: d.op = O_MOVF; mx = std::max(r, s); break;
            case 13: d.op = O_MOVT; mx = std::max(r, s); break;
            case 14: d.op = O_RUR; d.imm = (s << 4) | t; mx = r; break;
            case 15: d.op = O_WUR; d.imm = (r << 4) | s; mx = t; break;
            }
            break;
        case 4: case 5:
            d.op = O_EXTUI; d.imm = int(((op1 & 1) << 4) | s); d.target = op2 + 1; mx = std::max(r, t);
            break;
        case 8:                                         // LSCX
            mx = std::max(s, t);
            if (op2 == 0) d.op = O_LSX;
            else if (op2 == 1) d.op = O_LSXU;
            else if (op2 == 4) d.op = O_SSX;
            else if (op2 == 5) d.op = O_SSXU;
            break;
        case 10:                                        // FP0
            mx = 0;
            switch (op2) {
            case 0: d.op = O_ADDS; break;
            case 1: d.op = O_SUBS; break;
            case 2: d.op = O_MULS; break;
            case 4: d.op = O_MADDS; break;
            case 5: d.op = O_MSUBS; break;
            case 8: d.op = O_ROUNDS; mx = r; break;
            case 9: d.op = O_TRUNCS; mx = r; break;
            case 10: d.op = O_FLOORS; mx = r; break;
            case 11: d.op = O_CEILS; mx = r; break;
            case 12: d.op = O_FLOATS; mx = s; break;
            case 13: d.op = O_UFLOATS; mx = s; break;
            case 14: d.op = O_UTRUNCS; mx = r; break;
            case 15:
                switch (t) {
                case 0: d.op = O_MOVS; break;
                case 1: d.op = O_ABSS; break;
                case 4: d.op = O_RFR; mx = r; break;
                case 5: d.op = O_WFR; mx = s; break;
                case 6: d.op = O_NEGS; break;
                }
                break;
            }
            break;
        case 11:                                        // FP1
            mx = 0;
            switch (op2) {
            case 1: d.op = O_UNS; break;
            case 2: d.op = O_OEQS; break;
            case 3: d.op = O_UEQS; break;
            case 4: d.op = O_OLTS; break;
            case 5: d.op = O_ULTS; break;
            case 6: d.op = O_OLES; break;
            case 7: d.op = O_ULES; break;
            case 8: d.op = O_MOVEQZS; mx = t; break;
            case 9: d.op = O_MOVNEZS; mx = t; break;
            case 10: d.op = O_MOVLTZS; mx = t; break;
            case 11: d.op = O_MOVGEZS; mx = t; break;
            case 12: d.op = O_MOVFS; break;
            case 13: d.op = O_MOVTS; break;
            }
            break;
        }
        break;
    case 1:                                             // L32R
        d.op = O_L32R;
        d.target = ((pc + 3) & ~3u) + (0xfffc0000u | (((w >> 8) & 0xffff) << 2));
        mx = t;
        break;
    case 2:                                             // LSAI
        mx = std::max(s, t);
        switch (r) {
        case 0: d.op = O_L8UI; d.imm = int(imm8); break;
        case 1: d.op = O_L16UI; d.imm = int(imm8 << 1); break;
        case 2: d.op = O_L32I; d.imm = int(imm8 << 2); break;
        case 4: d.op = O_S8I; d.imm = int(imm8); break;
        case 5: d.op = O_S16I; d.imm = int(imm8 << 1); break;
        case 6: d.op = O_S32I; d.imm = int(imm8 << 2); break;
        case 7: d.op = O_NOP; break;                    // cache ops
        case 9: d.op = O_L16SI; d.imm = int(imm8 << 1); break;
        case 10: d.op = O_MOVI; d.imm = sext((s << 8) | imm8, 12); mx = t; break;
        case 11: d.op = O_L32I; d.imm = int(imm8 << 2); break;      // L32AI
        case 12: d.op = O_ADDI; d.imm = sext(imm8, 8); break;
        case 13: d.op = O_ADDMI; d.imm = sext(imm8, 8) * 256; break;
        case 15: d.op = O_S32I; d.imm = int(imm8 << 2); break;      // S32RI
        }
        break;
    case 3:                                             // LSCI
        mx = s;
        if (r == 0) d.op = O_LSI;
        else if (r == 4) d.op = O_SSI;
        else if (r == 8) d.op = O_LSIU;
        else if (r == 12) d.op = O_SSIU;
        d.imm = int(imm8 << 2);
        break;
    case 5: {                                           // CALLN
        const int n = (w >> 4) & 3;
        d.op = uint8_t(O_CALL0 + n);
        d.target = (pc & ~3u) + uint32_t(sext((w >> 6) & 0x3ffff, 18) << 2) + 4;
        mx = n * 4;
        break;
    }
    case 6: {                                           // SI
        const int n = (w >> 4) & 3, m = (w >> 6) & 3;
        const uint32_t imm12 = (w >> 12) & 0xfff;
        mx = s;
        if (n == 0) { d.op = O_J; d.target = pc + 4 + uint32_t(sext((w >> 6) & 0x3ffff, 18)); mx = 0; }
        else if (n == 1) { d.op = uint8_t(O_BEQZ + m); d.target = pc + 4 + uint32_t(sext(imm12, 12)); }
        else if (n == 2) { d.op = uint8_t(O_BEQI + m); d.imm = kB4[r]; d.target = pc + 4 + uint32_t(sext(imm8, 8)); }
        else {
            if (m == 0) { d.op = O_ENTRY; d.imm = int(imm12 << 3); }
            else if (m == 1) {
                mx = 0;
                if (r == 0) { d.op = O_BF; d.target = pc + 4 + uint32_t(sext(imm8, 8)); }
                else if (r == 1) { d.op = O_BT; d.target = pc + 4 + uint32_t(sext(imm8, 8)); }
                else if (r >= 8 && r <= 10) { d.op = uint8_t(O_LOOP + (r - 8)); d.target = pc + 4 + imm8; mx = s; }
            } else if (m == 2) { d.op = O_BLTUI; d.imm = int(kB4u[r]); d.target = pc + 4 + uint32_t(sext(imm8, 8)); }
            else { d.op = O_BGEUI; d.imm = int(kB4u[r]); d.target = pc + 4 + uint32_t(sext(imm8, 8)); }
        }
        break;
    }
    case 7: {                                           // B
        static const uint8_t ops[16] = {O_BNONE, O_BEQ, O_BLT, O_BLTU, O_BALL, O_BBC, O_BBCI, O_BBCI,
                                        O_BANY, O_BNE, O_BGE, O_BGEU, O_BNALL, O_BBS, O_BBSI, O_BBSI};
        d.op = ops[r];
        d.target = pc + 4 + uint32_t(sext(imm8, 8));
        if (d.op == O_BBCI || d.op == O_BBSI) { d.imm = int(((r & 1) << 4) | t); mx = s; }
        else mx = std::max(s, t);
        break;
    }
    }
    d.maxq = uint8_t(mx >> 2);
}

// ---- symbols / profile ----
struct Sym { uint32_t addr, size; std::string name; };
std::vector<Sym> g_syms;
std::vector<uint64_t> g_prof_cyc, g_prof_ins, g_prof_miss;     // per irom byte (counted only)

const Sym* sym_at(uint32_t a)
{
    auto it = std::upper_bound(g_syms.begin(), g_syms.end(), a, [](uint32_t x, const Sym& s) { return x < s.addr; });
    if (it == g_syms.begin()) return nullptr;
    --it;
    return a < it->addr + std::max(it->size, 1u) ? &*it : &*it;
}

// ---- host side ----
FILE* g_hash = nullptr;
std::vector<int> g_frame_cyc;
uint32_t g_frame = 0, g_skip = 0;
uint64_t g_im0 = 0, g_imiss = 0, g_dm0 = 0, g_dmiss = 0, g_ovf0 = 0, g_ovfc = 0;
uint64_t g_frame_start = 0, g_total = 0, g_total_ins = 0, g_ins = 0;
uint32_t g_audio_crc = 0, g_audio_n = 0;
bool g_done = false;
uint32_t g_crc_tab[256];

uint32_t crc(const uint8_t* p, size_t n)
{
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) c = g_crc_tab[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return ~c;
}

uint8_t* sim_ptr(uint32_t a) { Kind k; return host_ptr(a, k); }

uint32_t mb(int i) { uint32_t v; std::memcpy(&v, &g_host[0x1000 + 4 * i], 4); return v; }

void door(uint32_t cmd)
{
    switch (cmd) {
    case 1:
        g_count = g_frame >= g_skip;
        g_frame_start = g_cyc;
        g_ins = 0;
        g_im0 = g_ic.misses; g_dm0 = g_dc.misses; g_ovf0 = g_ovf + g_unf;
        break;
    case 2: {
        if (g_count) {
            g_frame_cyc.push_back(int(g_cyc - g_frame_start));
            g_total += g_cyc - g_frame_start;
            g_total_ins += g_ins;
            g_imiss += g_ic.misses - g_im0; g_dmiss += g_dc.misses - g_dm0; g_ovfc += g_ovf + g_unf - g_ovf0;
        }
        g_count = false;
        const uint32_t a = mb(0), n = mb(1);
        g_audio_crc = crc(sim_ptr(a), n * 2);
        g_audio_n = n;
        if (!g_hash) ++g_frame;
        if (g_frame % 500 == 0 && !g_hash) std::fprintf(stderr, "frame %u\n", g_frame);
        break;
    }
    case 3: {
        const uint32_t w = mb(0), h = mb(1);
        std::vector<uint8_t> fb(w * h * 2);
        uint32_t fbaddr; std::memcpy(&fbaddr, &g_host[16 + 4 + 4 + 64 + 4 + 4 + 4], 4);
        std::memcpy(fb.data(), sim_ptr(fbaddr), fb.size());
        if (g_hash) std::fprintf(g_hash, "%u %08x %08x %u\n", g_frame, crc(fb.data(), fb.size()), g_audio_crc, g_audio_n);
        ++g_frame;
        if (g_frame % 500 == 0) std::fprintf(stderr, "frame %u\n", g_frame);
        break;
    }
    case 4: break;
    case 7: std::fprintf(stderr, "machine + allocations: %u bytes\n", mb(0)); break;
    case 8: die("target error", mb(0), g_pc);
    case 9: g_done = true; break;
    }
}

bool read_file(const std::string& p, std::vector<uint8_t>& out)
{
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(size_t(n));
    bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

void load_elf(const std::string& path)
{
    std::vector<uint8_t> e;
    if (!read_file(path, e)) die("can't read elf");
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, &e[o], 4); return v; };
    auto u16 = [&](size_t o) { uint16_t v; std::memcpy(&v, &e[o], 2); return uint32_t(v); };
    const uint32_t phoff = u32(28), shoff = u32(32);
    const uint32_t phnum = u16(44), shentsize = u16(46), shnum = u16(48);
    g_pc = u32(24);
    for (uint32_t i = 0; i < phnum; ++i) {
        const size_t ph = phoff + i * 32;
        if (u32(ph) != 1) continue;
        const uint32_t off = u32(ph + 4), va = u32(ph + 8), fsz = u32(ph + 16);
        for (uint32_t k = 0; k < fsz; ++k) *sim_ptr(va + k) = e[off + k];
    }
    // symbols
    for (uint32_t i = 0; i < shnum; ++i) {
        const size_t sh = shoff + i * shentsize;
        if (u32(sh + 4) != 2) continue;             // SHT_SYMTAB
        const uint32_t off = u32(sh + 16), size = u32(sh + 20), link = u32(sh + 24);
        const size_t strsh = shoff + link * shentsize;
        const uint32_t stroff = u32(strsh + 16);
        for (uint32_t k = 0; k < size; k += 16) {
            const uint32_t name = u32(off + k), value = u32(off + k + 4), sz = u32(off + k + 8);
            const uint8_t info = e[off + k + 12];
            if ((info & 15) != 2) continue;             // STT_FUNC
            if (value < kIrom || value >= kIrom + kIromSize) continue;
            g_syms.push_back({value, sz, std::string(reinterpret_cast<const char*>(&e[stroff + name]))});
        }
    }
    std::sort(g_syms.begin(), g_syms.end(), [](const Sym& a, const Sym& b) { return a.addr < b.addr; });
    for (const Sym& s : g_syms) if (s.name == "__divsf3") g_divsf3 = s.addr;
}

// ---- execution ----
inline void ovf_check(int maxq)
{
    // registers of quads WB+1 .. WB+maxq in use by an older frame: spill it
    for (int q = 1; q <= maxq; ++q) {
        const int pq = (g_wb + q) & 15;
        if (g_ws & (1u << pq)) {
            g_ws &= ~(1u << pq);
            ++g_ovf;
            g_extra += P_OVF;
        }
    }
}


int32_t to_int(float f, int scale, int mode, bool uns)
{
    double x = double(f) * std::ldexp(1.0, scale);
    if (std::isnan(x)) return uns ? -1 : 0x7fffffff;
    double r;
    switch (mode) {
    case 0: r = std::nearbyint(x); break;
    case 1: r = std::trunc(x); break;
    case 2: r = std::floor(x); break;
    default: r = std::ceil(x); break;
    }
    if (uns) {
        if (r >= 4294967296.0) return int32_t(0xffffffffu);
        if (r < 0) return 0;     // (hardware: saturates / wraps - not seen)
        return int32_t(uint32_t(r));
    }
    if (r >= 2147483648.0) return 0x7fffffff;
    if (r < -2147483648.0) return int32_t(0x80000000u);
    return int32_t(r);
}

uint32_t nsau(uint32_t v) { return v ? uint32_t(__builtin_clz(v)) : 32; }
uint32_t nsa(uint32_t v)
{
    const uint32_t x = (v & 0x80000000u) ? ~v : v;
    return x ? uint32_t(__builtin_clz(x)) - 1 : 31;
}

void run()
{
    g_ws = 1u << (g_wb & 15);
    uint64_t steps = 0;
    while (!g_done) {
        const uint32_t pc = g_pc;
        if (pc - kIrom >= kIromSize) die("pc out of text", pc, pc);
        if (pc == g_divsf3) {
            // __divsf3 (the FPU's division sequence, not simulated): done
            // here, correctly rounded, at a fixed cost; returns to the caller
            uint32_t* ca = A() + g_callinc * 4;
            const float q = fbits(ca[2]) / fbits(ca[3]);
            ca[2] = bitsf(q);
            const uint32_t ra = ca[0];
            g_pc = (ra & 0x3fffffffu) | (pc & 0xc0000000u);
            g_cyc += uint64_t(P_FDIV);
            if (g_count) { g_prof_cyc[pc - kIrom] += uint64_t(P_FDIV); ++g_prof_ins[pc - kIrom]; g_ins += 20; }
            continue;
        }
        Dec& d = g_dec[pc - kIrom];
        if (!d.decoded) decode(pc, d);
        const uint64_t c0 = g_cyc;
        uint64_t issue = g_cyc;
        g_extra = 0;
        // instruction fetch through the cache
        {
            const uint32_t l0 = pc >> 5, l1 = (pc + d.len - 1) >> 5;
            if (l0 != g_last_iline) { if (!g_ic.access(pc)) { g_extra += P_ICMISS; if (g_count) ++g_prof_miss[pc - kIrom]; } g_last_iline = l0; }
            if (l1 != l0) { if (!g_ic.access(pc + d.len - 1)) { g_extra += P_ICMISS; if (g_count) ++g_prof_miss[pc - kIrom]; } g_last_iline = l1; }
        }
        if (d.maxq) ovf_check(d.maxq);
        uint32_t* a = A();
        uint64_t* rd = g_rdy + g_wb * 4;
        const int r = d.r, s = d.s, t = d.t;
        uint32_t next = pc + d.len;
        bool taken = false;
        int extra = 0;
#define USE(x) do { if (rd[x] > issue) issue = rd[x]; } while (0)
#define FUSE(x) do { if (g_frdy[x] > issue) issue = g_frdy[x]; } while (0)
#define DEF(x, l) do { rd[x] = issue + (l); } while (0)
#define FDEF(x, l) do { g_frdy[x] = issue + (l); } while (0)
#define BR(c) do { if (c) { next = d.target; taken = true; } } while (0)
        switch (d.op) {
        case O_NOP: break;
        case O_ADD: case O_ADDN: USE(s); USE(t); a[r] = a[s] + a[t]; DEF(r, 1); break;
        case O_ADDX2: USE(s); USE(t); a[r] = (a[s] << 1) + a[t]; DEF(r, 1); break;
        case O_ADDX4: USE(s); USE(t); a[r] = (a[s] << 2) + a[t]; DEF(r, 1); break;
        case O_ADDX8: USE(s); USE(t); a[r] = (a[s] << 3) + a[t]; DEF(r, 1); break;
        case O_SUB: USE(s); USE(t); a[r] = a[s] - a[t]; DEF(r, 1); break;
        case O_SUBX2: USE(s); USE(t); a[r] = (a[s] << 1) - a[t]; DEF(r, 1); break;
        case O_SUBX4: USE(s); USE(t); a[r] = (a[s] << 2) - a[t]; DEF(r, 1); break;
        case O_SUBX8: USE(s); USE(t); a[r] = (a[s] << 3) - a[t]; DEF(r, 1); break;
        case O_AND: USE(s); USE(t); a[r] = a[s] & a[t]; DEF(r, 1); break;
        case O_OR: USE(s); USE(t); a[r] = a[s] | a[t]; DEF(r, 1); break;
        case O_XOR: USE(s); USE(t); a[r] = a[s] ^ a[t]; DEF(r, 1); break;
        case O_SSR: USE(s); g_sar = a[s] & 31; break;
        case O_SSL: USE(s); g_sar = 32 - (a[s] & 31); break;
        case O_SSA8L: USE(s); g_sar = (a[s] & 3) * 8; break;
        case O_SSA8B: USE(s); g_sar = 32 - (a[s] & 3) * 8; break;
        case O_SSAI: g_sar = uint32_t(d.imm); break;
        case O_NSA: USE(s); a[t] = nsa(a[s]); DEF(t, 1); break;
        case O_NSAU: USE(s); a[t] = nsau(a[s]); DEF(t, 1); break;
        case O_NEG: USE(t); a[r] = 0u - a[t]; DEF(r, 1); break;
        case O_ABS: USE(t); a[r] = int32_t(a[t]) < 0 ? 0u - a[t] : a[t]; DEF(r, 1); break;
        case O_SLLI: USE(s); a[r] = d.imm >= 32 ? 0 : a[s] << d.imm; DEF(r, 1); break;
        case O_SRAI: USE(t); a[r] = uint32_t(int32_t(a[t]) >> d.imm); DEF(r, 1); break;
        case O_SRLI: USE(t); a[r] = a[t] >> d.imm; DEF(r, 1); break;
        case O_SRC: USE(s); USE(t); a[r] = uint32_t(((uint64_t(a[s]) << 32) | a[t]) >> g_sar); DEF(r, 1); break;
        case O_SRL: USE(t); a[r] = uint32_t(uint64_t(a[t]) >> g_sar); DEF(r, 1); break;
        case O_SLL: USE(s); a[r] = uint32_t((uint64_t(a[s]) << 32) >> g_sar); DEF(r, 1); break;
        case O_SRA: USE(t); a[r] = uint32_t(int64_t(int32_t(a[t])) >> g_sar); DEF(r, 1); break;
        case O_MUL16U: USE(s); USE(t); a[r] = (a[s] & 0xffff) * (a[t] & 0xffff); DEF(r, P_MUL); break;
        case O_MUL16S: USE(s); USE(t); a[r] = uint32_t(int32_t(int16_t(a[s])) * int32_t(int16_t(a[t]))); DEF(r, P_MUL); break;
        case O_SALTU: USE(s); USE(t); a[r] = a[s] < a[t]; DEF(r, 1); break;
        case O_SALT: USE(s); USE(t); a[r] = int32_t(a[s]) < int32_t(a[t]); DEF(r, 1); break;
        case O_MULL: USE(s); USE(t); a[r] = a[s] * a[t]; DEF(r, P_MUL); break;
        case O_MULUH: USE(s); USE(t); a[r] = uint32_t((uint64_t(a[s]) * a[t]) >> 32); DEF(r, P_MUL); break;
        case O_MULSH: USE(s); USE(t); a[r] = uint32_t(uint64_t(int64_t(int32_t(a[s])) * int32_t(a[t])) >> 32); DEF(r, P_MUL); break;
        case O_QUOU: USE(s); USE(t); if (!a[t]) die("div0", 0, pc); a[r] = a[s] / a[t]; DEF(r, P_DIV); break;
        case O_QUOS: USE(s); USE(t); if (!a[t]) die("div0", 0, pc); a[r] = uint32_t(int32_t(a[s]) / int32_t(a[t])); DEF(r, P_DIV); break;
        case O_REMU: USE(s); USE(t); if (!a[t]) die("div0", 0, pc); a[r] = a[s] % a[t]; DEF(r, P_DIV); break;
        case O_REMS: USE(s); USE(t); if (!a[t]) die("div0", 0, pc); a[r] = uint32_t(int32_t(a[s]) % int32_t(a[t])); DEF(r, P_DIV); break;
        case O_ANDB: g_br = (g_br & ~(1u << r)) | ((((g_br >> s) & (g_br >> t)) & 1) << r); break;
        case O_ANDBC: g_br = (g_br & ~(1u << r)) | ((((g_br >> s) & ~(g_br >> t)) & 1) << r); break;
        case O_ORB: g_br = (g_br & ~(1u << r)) | ((((g_br >> s) | (g_br >> t)) & 1) << r); break;
        case O_ORBC: g_br = (g_br & ~(1u << r)) | ((((g_br >> s) | ~(g_br >> t)) & 1) << r); break;
        case O_XORB: g_br = (g_br & ~(1u << r)) | ((((g_br >> s) ^ (g_br >> t)) & 1) << r); break;
        case O_ANY4: g_br = (g_br & ~(1u << t)) | (((g_br >> s) & 15) ? 1u << t : 0); break;
        case O_ALL4: g_br = (g_br & ~(1u << t)) | (((g_br >> s) & 15) == 15 ? 1u << t : 0); break;
        case O_ANY8: g_br = (g_br & ~(1u << t)) | (((g_br >> s) & 255) ? 1u << t : 0); break;
        case O_ALL8: g_br = (g_br & ~(1u << t)) | (((g_br >> s) & 255) == 255 ? 1u << t : 0); break;
        case O_RSR: case O_XSR: {
            uint32_t v = 0;
            switch (d.imm) {
            case 0: v = g_lbeg; break; case 1: v = g_lend; break; case 2: v = g_lcount; break;
            case 3: v = g_sar; break; case 4: v = g_br; break; case 230: v = g_ps; break;
            case 234: v = uint32_t(g_cyc); break;
            }
            if (d.op == O_XSR) {
                USE(t);
                const uint32_t w = a[t];
                switch (d.imm) {
                case 0: g_lbeg = w; break; case 1: g_lend = w; break; case 2: g_lcount = w; break;
                case 3: g_sar = w & 63; break; case 4: g_br = w; break; case 230: g_ps = w; break;
                }
            }
            a[t] = v; DEF(t, 1);
            break;
        }
        case O_WSR: {
            USE(t);
            const uint32_t w = a[t];
            switch (d.imm) {
            case 0: g_lbeg = w; break; case 1: g_lend = w; break; case 2: g_lcount = w; break;
            case 3: g_sar = w & 63; break; case 4: g_br = w; break; case 230: g_ps = w; break;
            }
            break;
        }
        case O_SEXT: USE(s); a[r] = uint32_t(sext(a[s], t + 8)); DEF(r, 1); break;
        case O_CLAMPS: {
            USE(s);
            const int32_t lo = -(1 << (t + 7)), hi = (1 << (t + 7)) - 1, v = int32_t(a[s]);
            a[r] = uint32_t(v < lo ? lo : v > hi ? hi : v); DEF(r, 1); break;
        }
        case O_MIN: USE(s); USE(t); a[r] = int32_t(a[s]) < int32_t(a[t]) ? a[s] : a[t]; DEF(r, 1); break;
        case O_MAX: USE(s); USE(t); a[r] = int32_t(a[s]) > int32_t(a[t]) ? a[s] : a[t]; DEF(r, 1); break;
        case O_MINU: USE(s); USE(t); a[r] = a[s] < a[t] ? a[s] : a[t]; DEF(r, 1); break;
        case O_MAXU: USE(s); USE(t); a[r] = a[s] > a[t] ? a[s] : a[t]; DEF(r, 1); break;
        case O_MOVEQZ: USE(s); USE(t); USE(r); if (a[t] == 0) a[r] = a[s]; DEF(r, 1); break;
        case O_MOVNEZ: USE(s); USE(t); USE(r); if (a[t] != 0) a[r] = a[s]; DEF(r, 1); break;
        case O_MOVLTZ: USE(s); USE(t); USE(r); if (int32_t(a[t]) < 0) a[r] = a[s]; DEF(r, 1); break;
        case O_MOVGEZ: USE(s); USE(t); USE(r); if (int32_t(a[t]) >= 0) a[r] = a[s]; DEF(r, 1); break;
        case O_MOVF: USE(s); USE(r); if (!((g_br >> t) & 1)) a[r] = a[s]; DEF(r, 1); break;
        case O_MOVT: USE(s); USE(r); if ((g_br >> t) & 1) a[r] = a[s]; DEF(r, 1); break;
        case O_RUR: a[r] = 0; DEF(r, 1); break;
        case O_WUR: break;
        case O_EXTUI: USE(t); a[r] = (a[t] >> d.imm) & (d.target >= 32 ? 0xffffffffu : (1u << d.target) - 1); DEF(r, 1); break;
        case O_RET: case O_RETN: USE(0); next = a[0]; taken = true; break;
        case O_RETW: case O_RETWN: {
            USE(0);
            const uint32_t ra = a[0];
            const int n = int(ra >> 30);
            if (n == 0) die("retw with call0 return address", ra, pc);
            next = (ra & 0x3fffffffu) | (pc & 0xc0000000u);
            const int caller = (g_wb - n) & 15;
            if (!(g_ws & (1u << caller))) { ++g_unf; g_extra += P_OVF; g_ws |= 1u << caller; }
            g_ws &= ~(1u << (g_wb & 15));
            g_wb -= n;
            if (g_wb < 1) die("window stack underflow", 0, pc);
            taken = true;
            break;
        }
        case O_JX: USE(s); next = a[s]; taken = true; break;
        case O_CALLX0: USE(s); next = a[s]; a[0] = pc + 3; DEF(0, 1); taken = true; break;
        case O_CALLX4: case O_CALLX8: case O_CALLX12: {
            USE(s);
            const int n = d.op - O_CALLX0;
            next = a[s];
            a[n * 4] = (uint32_t(n) << 30) | ((pc + 3) & 0x3fffffffu);
            DEF(n * 4, 1);
            g_callinc = n;
            taken = true;
            break;
        }
        case O_CALL0: next = d.target; a[0] = pc + 3; DEF(0, 1); taken = true; break;
        case O_CALL4: case O_CALL8: case O_CALL12: {
            const int n = d.op - O_CALL0;
            next = d.target;
            a[n * 4] = (uint32_t(n) << 30) | ((pc + 3) & 0x3fffffffu);
            DEF(n * 4, 1);
            g_callinc = n;
            taken = true;
            break;
        }
        case O_ENTRY: {
            USE(s);
            const uint32_t v = a[s] - uint32_t(d.imm);
            g_wb += g_callinc;
            if (g_wb * 4 + 16 >= int(sizeof g_ar / 4)) die("window stack overflow", 0, pc);
            g_ar[g_wb * 4 + s] = v;
            g_rdy[g_wb * 4 + s] = issue + 1;
            // the new frame owns its quad; an older frame there is spilled
            const int pq = g_wb & 15;
            if (g_ws & (1u << pq)) { ++g_ovf; g_extra += P_OVF; }
            g_ws |= 1u << pq;
            // (and the quads of a4-a15 if the callee uses them: checked as they are used)
            break;
        }
        case O_MOVSP: USE(s); a[t] = a[s]; DEF(t, 1); break;
        case O_BREAK: die("break", 0, pc);
        case O_SIMCALL: break;
        case O_RSIL: a[t] = g_ps; g_ps = (g_ps & ~15u) | s; DEF(t, 1); break;
        case O_L32R: a[t] = ld32(d.target); DEF(t, P_LD); break;
        case O_L8UI: USE(s); a[t] = ld8(a[s] + uint32_t(d.imm)); DEF(t, P_LD); break;
        case O_L16UI: USE(s); a[t] = ld16(a[s] + uint32_t(d.imm)); DEF(t, P_LD); break;
        case O_L16SI: USE(s); a[t] = uint32_t(int32_t(int16_t(ld16(a[s] + uint32_t(d.imm))))); DEF(t, P_LD); break;
        case O_L32I: USE(s); a[t] = ld32(a[s] + uint32_t(d.imm)); DEF(t, P_LD); break;
        case O_S8I: USE(s); USE(t); st8(a[s] + uint32_t(d.imm), a[t]); break;
        case O_S16I: USE(s); USE(t); st16(a[s] + uint32_t(d.imm), a[t]); break;
        case O_S32I: USE(s); USE(t); st32(a[s] + uint32_t(d.imm), a[t]); break;
        case O_MOVI: case O_MOVIN: a[d.op == O_MOVI ? t : s] = uint32_t(d.imm); DEF(d.op == O_MOVI ? t : s, 1); break;
        case O_ADDI: USE(s); a[t] = a[s] + uint32_t(d.imm); DEF(t, 1); break;
        case O_ADDMI: USE(s); a[t] = a[s] + uint32_t(d.imm); DEF(t, 1); break;
        case O_ADDIN: USE(s); a[r] = a[s] + uint32_t(d.imm); DEF(r, 1); break;
        case O_MOVN: USE(s); a[t] = a[s]; DEF(t, 1); break;
        case O_LSI: USE(s); g_f[t] = fbits(ld32(a[s] + uint32_t(d.imm))); FDEF(t, P_LD); break;
        case O_LSIU: USE(s); a[s] += uint32_t(d.imm); g_f[t] = fbits(ld32(a[s])); FDEF(t, P_LD); DEF(s, 1); break;
        case O_SSI: USE(s); FUSE(t); st32(a[s] + uint32_t(d.imm), bitsf(g_f[t])); break;
        case O_SSIU: USE(s); FUSE(t); a[s] += uint32_t(d.imm); st32(a[s], bitsf(g_f[t])); DEF(s, 1); break;
        case O_LSX: USE(s); USE(t); g_f[r] = fbits(ld32(a[s] + a[t])); FDEF(r, P_LD); break;
        case O_LSXU: USE(s); USE(t); a[s] += a[t]; g_f[r] = fbits(ld32(a[s])); FDEF(r, P_LD); DEF(s, 1); break;
        case O_SSX: USE(s); USE(t); FUSE(r); st32(a[s] + a[t], bitsf(g_f[r])); break;
        case O_SSXU: USE(s); USE(t); FUSE(r); a[s] += a[t]; st32(a[s], bitsf(g_f[r])); DEF(s, 1); break;
        case O_J: next = d.target; taken = true; break;
        case O_BEQZ: case O_BEQZN: USE(s); BR(a[s] == 0); break;
        case O_BNEZ: case O_BNEZN: USE(s); BR(a[s] != 0); break;
        case O_BLTZ: USE(s); BR(int32_t(a[s]) < 0); break;
        case O_BGEZ: USE(s); BR(int32_t(a[s]) >= 0); break;
        case O_BEQI: USE(s); BR(int32_t(a[s]) == d.imm); break;
        case O_BNEI: USE(s); BR(int32_t(a[s]) != d.imm); break;
        case O_BLTI: USE(s); BR(int32_t(a[s]) < d.imm); break;
        case O_BGEI: USE(s); BR(int32_t(a[s]) >= d.imm); break;
        case O_BLTUI: USE(s); BR(a[s] < uint32_t(d.imm)); break;
        case O_BGEUI: USE(s); BR(a[s] >= uint32_t(d.imm)); break;
        case O_BF: BR(!((g_br >> s) & 1)); break;
        case O_BT: BR((g_br >> s) & 1); break;
        case O_LOOP: case O_LOOPNEZ: case O_LOOPGTZ:
            USE(s);
            g_lcount = a[s] - 1;
            g_lbeg = pc + 3;
            g_lend = d.target;
            if ((d.op == O_LOOPNEZ && a[s] == 0) || (d.op == O_LOOPGTZ && int32_t(a[s]) <= 0)) { next = g_lend; taken = true; }
            extra += 1;
            break;
        case O_BNONE: USE(s); USE(t); BR((a[s] & a[t]) == 0); break;
        case O_BEQ: USE(s); USE(t); BR(a[s] == a[t]); break;
        case O_BLT: USE(s); USE(t); BR(int32_t(a[s]) < int32_t(a[t])); break;
        case O_BLTU: USE(s); USE(t); BR(a[s] < a[t]); break;
        case O_BALL: USE(s); USE(t); BR((~a[s] & a[t]) == 0); break;
        case O_BBC: USE(s); USE(t); BR(!((a[s] >> (a[t] & 31)) & 1)); break;
        case O_BBCI: USE(s); BR(!((a[s] >> d.imm) & 1)); break;
        case O_BANY: USE(s); USE(t); BR((a[s] & a[t]) != 0); break;
        case O_BNE: USE(s); USE(t); BR(a[s] != a[t]); break;
        case O_BGE: USE(s); USE(t); BR(int32_t(a[s]) >= int32_t(a[t])); break;
        case O_BGEU: USE(s); USE(t); BR(a[s] >= a[t]); break;
        case O_BNALL: USE(s); USE(t); BR((~a[s] & a[t]) != 0); break;
        case O_BBS: USE(s); USE(t); BR((a[s] >> (a[t] & 31)) & 1); break;
        case O_BBSI: USE(s); BR((a[s] >> d.imm) & 1); break;
        // floating point
        case O_ADDS: FUSE(s); FUSE(t); g_f[r] = g_f[s] + g_f[t]; FDEF(r, P_FP); break;
        case O_SUBS: FUSE(s); FUSE(t); g_f[r] = g_f[s] - g_f[t]; FDEF(r, P_FP); break;
        case O_MULS: FUSE(s); FUSE(t); g_f[r] = g_f[s] * g_f[t]; FDEF(r, P_FP); break;
        case O_MADDS: FUSE(s); FUSE(t); FUSE(r); g_f[r] = std::fmaf(g_f[s], g_f[t], g_f[r]); FDEF(r, P_FP); break;
        case O_MSUBS: FUSE(s); FUSE(t); FUSE(r); g_f[r] = std::fmaf(-g_f[s], g_f[t], g_f[r]); FDEF(r, P_FP); break;
        case O_ROUNDS: FUSE(s); a[r] = uint32_t(to_int(g_f[s], t, 0, false)); DEF(r, P_FP); break;
        case O_TRUNCS: FUSE(s); a[r] = uint32_t(to_int(g_f[s], t, 1, false)); DEF(r, P_FP); break;
        case O_FLOORS: FUSE(s); a[r] = uint32_t(to_int(g_f[s], t, 2, false)); DEF(r, P_FP); break;
        case O_CEILS: FUSE(s); a[r] = uint32_t(to_int(g_f[s], t, 3, false)); DEF(r, P_FP); break;
        case O_UTRUNCS: FUSE(s); a[r] = uint32_t(to_int(g_f[s], t, 1, true)); DEF(r, P_FP); break;
        case O_FLOATS: USE(s); g_f[r] = std::ldexp(float(int32_t(a[s])), -t); FDEF(r, P_FP); break;
        case O_UFLOATS: USE(s); g_f[r] = std::ldexp(float(a[s]), -t); FDEF(r, P_FP); break;
        case O_MOVS: FUSE(s); g_f[r] = g_f[s]; FDEF(r, 1); break;
        case O_ABSS: FUSE(s); g_f[r] = fbits(bitsf(g_f[s]) & 0x7fffffffu); FDEF(r, 1); break;
        case O_NEGS: FUSE(s); g_f[r] = fbits(bitsf(g_f[s]) ^ 0x80000000u); FDEF(r, 1); break;
        case O_RFR: FUSE(s); a[r] = bitsf(g_f[s]); DEF(r, 1); break;
        case O_WFR: USE(s); g_f[r] = fbits(a[s]); FDEF(r, 1); break;
#define FCMP(c) do { FUSE(s); FUSE(t); const float x = g_f[s], y = g_f[t]; g_br = (g_br & ~(1u << r)) | ((c) ? 1u << r : 0); } while (0)
        case O_UNS: FCMP(std::isnan(x) || std::isnan(y)); break;
        case O_OEQS: FCMP(x == y); break;
        case O_UEQS: FCMP(std::isnan(x) || std::isnan(y) || x == y); break;
        case O_OLTS: FCMP(x < y); break;
        case O_ULTS: FCMP(std::isnan(x) || std::isnan(y) || x < y); break;
        case O_OLES: FCMP(x <= y); break;
        case O_ULES: FCMP(std::isnan(x) || std::isnan(y) || x <= y); break;
        case O_MOVEQZS: FUSE(s); USE(t); if (a[t] == 0) g_f[r] = g_f[s]; FDEF(r, 1); break;
        case O_MOVNEZS: FUSE(s); USE(t); if (a[t] != 0) g_f[r] = g_f[s]; FDEF(r, 1); break;
        case O_MOVLTZS: FUSE(s); USE(t); if (int32_t(a[t]) < 0) g_f[r] = g_f[s]; FDEF(r, 1); break;
        case O_MOVGEZS: FUSE(s); USE(t); if (int32_t(a[t]) >= 0) g_f[r] = g_f[s]; FDEF(r, 1); break;
        case O_MOVFS: FUSE(s); if (!((g_br >> t) & 1)) g_f[r] = g_f[s]; FDEF(r, 1); break;
        case O_MOVTS: FUSE(s); if ((g_br >> t) & 1) g_f[r] = g_f[s]; FDEF(r, 1); break;
        default:
            die("unimplemented instruction", g_irom[pc - kIrom] | (g_irom[pc - kIrom + 1] << 8) | (g_irom[pc - kIrom + 2] << 16), pc);
        }
        // zero-overhead loop
        if (!taken && next == g_lend && g_lcount != 0) {
            --g_lcount;
            next = g_lbeg;
        }
        if (taken) extra += P_BR;
        g_cyc = issue + 1 + uint64_t(extra) + g_extra;
        g_pc = next;
        if (g_count) {
            ++g_ins;
            g_prof_cyc[pc - kIrom] += g_cyc - c0;
            ++g_prof_ins[pc - kIrom];
        }
        ++steps;
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 5) {
        std::fprintf(stderr, "usage: xsim game.elf DUMPDIR GAME FRAMES [--hash F] [--prof F] [--render] [--skip N] [--demo on|off] [--param k=v]\n");
        return 2;
    }
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        g_crc_tab[i] = c;
    }
    const std::string elf = argv[1], dir = argv[2], game = argv[3];
    const uint32_t frames = uint32_t(std::atoi(argv[4]));
    std::string hash_path, prof_path;
    bool render = false;
    int demo = -1;
    for (int i = 5; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--hash") hash_path = argv[++i];
        else if (a == "--prof") prof_path = argv[++i];
        else if (a == "--render") render = true;
        else if (a == "--skip") g_skip = uint32_t(std::atoi(argv[++i]));
        else if (a == "--demo") demo = std::string(argv[++i]) == "on";
        else if (a == "--param") {
            const std::string kv = argv[++i];
            const size_t eq = kv.find('=');
            const std::string k = kv.substr(0, eq);
            const int v = std::atoi(kv.c_str() + eq + 1);
            if (k == "br") P_BR = v; else if (k == "ovf") P_OVF = v; else if (k == "icmiss") P_ICMISS = v;
            else if (k == "dflash") P_DFLASH = v; else if (k == "dpsram") P_DPSRAM = v; else if (k == "ld") P_LD = v;
            else if (k == "mul") P_MUL = v; else if (k == "div") P_DIV = v; else if (k == "fp") P_FP = v;
            else if (k == "icache_kb") P_ICACHE_KB = v; else if (k == "ways") g_ic.ways = v; else if (k == "dcache_kb") P_DCACHE_KB = v;
            else { std::fprintf(stderr, "unknown param %s\n", k.c_str()); return 2; }
        }
    }
    g_ic.init(P_ICACHE_KB);
    g_dc.init(P_DCACHE_KB);
    map(kIrom, g_irom, K_FLASHI);
    map(kDrom, g_drom, K_FLASHD);
    map(kPsram, g_psram, K_PSRAM);
    map(kDram, g_dram, K_DRAM);
    map(kHost, g_host, K_HOST);
    g_dec.resize(kIromSize);
    g_prof_cyc.assign(kIromSize, 0);
    g_prof_ins.assign(kIromSize, 0);
    g_prof_miss.assign(kIromSize, 0);
    load_elf(elf);

    // setup page: id[16], frames, nreg, reg[16], inputs, rate, demo, fb, render
    std::memset(g_host.data(), 0, 0x2000);
    std::strncpy(reinterpret_cast<char*>(g_host.data()), game.c_str(), 15);
    uint32_t off = 16;
    auto put = [&](uint32_t v) { std::memcpy(&g_host[off], &v, 4); off += 4; };
    put(frames);
    std::vector<uint32_t> regs;
    uint32_t pa = kPsram;
    for (int r = 0; r < 16; ++r) {
        std::vector<uint8_t> data;
        if (!read_file(dir + "/r" + std::to_string(r) + ".bin", data)) break;
        std::memcpy(&g_psram[pa - kPsram], data.data(), data.size());
        regs.push_back(pa);
        pa += uint32_t((data.size() + 63) & ~size_t(63));
    }
    put(uint32_t(regs.size()));
    for (int r = 0; r < 16; ++r) put(r < int(regs.size()) ? regs[size_t(r)] : 0);
    std::vector<uint8_t> inputs;
    if (!read_file(dir + "/inputs.bin", inputs)) die("no inputs.bin");
    if (inputs.size() < size_t(frames) * 8) die("inputs.bin too short", uint32_t(inputs.size()));
    std::memcpy(&g_host[0x10000], inputs.data(), inputs.size());
    put(kHost + 0x10000);
    put(24000);
    put(uint32_t(demo));
    put(kHost + 0x800000);                  // frame buffer
    put(render || !hash_path.empty() ? 1 : 0);
    if (!hash_path.empty()) g_hash = std::fopen(hash_path.c_str(), "w");

    g_ar[g_wb * 4 + 0] = 0;
    run();
    if (g_hash) std::fclose(g_hash);

    const double n = double(g_frame_cyc.size());
    std::sort(g_frame_cyc.begin(), g_frame_cyc.end());
    std::printf("frames counted %zu: cycles a frame %.0f (%.2f ms at 240 MHz), median %d, worst %d; instructions %.0f (CPI %.2f)\n",
                g_frame_cyc.size(), g_total / n, g_total / n / 240000.0, g_frame_cyc.empty() ? 0 : g_frame_cyc[g_frame_cyc.size() / 2],
                g_frame_cyc.empty() ? 0 : g_frame_cyc.back(), g_total_ins / n, double(g_total) / double(g_total_ins));
    std::printf("icache %.4f%% miss (%llu), dcache %.4f%% miss (%llu), overflows %llu underflows %llu (all frames)\n",
                100.0 * g_ic.misses / double(g_ic.hits + g_ic.misses), (unsigned long long)g_ic.misses,
                100.0 * g_dc.misses / double(g_dc.hits + g_dc.misses + 1), (unsigned long long)g_dc.misses,
                (unsigned long long)g_ovf, (unsigned long long)g_unf);
    std::printf("per counted frame: icache misses %.0f, dcache misses %.0f, window spills %.0f, cycles less icache misses %.0f\n",
                g_imiss / n, g_dmiss / n, g_ovfc / n, (g_total - double(g_imiss) * P_ICMISS) / n);
    if (!prof_path.empty()) {
        FILE* f = std::fopen(prof_path.c_str(), "w");
        std::map<std::string, std::pair<uint64_t, uint64_t>> by;
        std::map<std::string, uint64_t> miss;
        uint64_t unk = 0;
        for (uint32_t i = 0; i < kIromSize; ++i) {
            if (!g_prof_cyc[i]) continue;
            const Sym* s = sym_at(kIrom + i);
            if (!s) { unk += g_prof_cyc[i]; continue; }
            auto& e = by[s->name];
            e.first += g_prof_cyc[i];
            e.second += g_prof_ins[i];
            miss[s->name] += g_prof_miss[i];
        }
        std::vector<std::pair<uint64_t, std::string>> v;
        for (auto& [k, e] : by) v.push_back({e.first, k});
        std::sort(v.rbegin(), v.rend());
        std::fprintf(f, "# cycles/frame instr/frame icache-misses/frame function  (frames %zu)\n", g_frame_cyc.size());
        for (auto& [c, k] : v)
            std::fprintf(f, "%10.0f %10.0f %8.1f  %s\n", c / n, by[k].second / n, miss[k] / n, k.c_str());
        std::fprintf(f, "%10.0f unknown\n", unk / n);
        std::fclose(f);
        // per address (hot spots)
        FILE* g = std::fopen((prof_path + ".pc").c_str(), "w");
        for (uint32_t i = 0; i < kIromSize; ++i)
            if (g_prof_cyc[i]) std::fprintf(g, "%08x %.1f %.1f\n", kIrom + i, g_prof_cyc[i] / n, g_prof_ins[i] / n);
        std::fclose(g);
    }
    return 0;
}
