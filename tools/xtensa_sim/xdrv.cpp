// Driver for the Xtensa simulator: runs a game frame by frame, talking to the
// host through a mailbox page (0x50000000: setup, 0x50001000: doorbell).
#include "emu/games.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

struct Setup {
    char     id[16];
    uint32_t frames;
    uint32_t nreg;
    uint32_t reg[16];
    uint32_t inputs;    // per frame: u16 held, i16 lx, i16 ly, u16 0
    uint32_t rate;
    int32_t  demo;      // -1 = leave
    uint32_t fb;        // frame buffer for render (host memory)
    uint32_t render;    // render each frame
};

struct Mailbox {
    volatile uint32_t a[8];
    volatile uint32_t cmd;
};

Setup&   S = *reinterpret_cast<Setup*>(0x50000000);
Mailbox& MB = *reinterpret_cast<Mailbox*>(0x50001000);

void call(uint32_t cmd) { MB.cmd = cmd; }

alignas(16) uint8_t g_heap[320 * 1024];
size_t g_used = 0;

void* bump(size_t n)
{
    n = (n + 15) & ~size_t(15);
    if (g_used + n > sizeof g_heap) return nullptr;
    void* p = g_heap + g_used;
    g_used += n;
    return p;
}
void nofree(void*) {}

int16_t g_audio[2048];

} // namespace

extern "C" {
void abort() { MB.a[0] = 0xdead; call(8); for (;;) {} }
void __assert_func(const char*, int line, const char*, const char*) { MB.a[0] = uint32_t(line); call(8); for (;;) {} }
void __cxa_pure_virtual() { abort(); }
int  __cxa_guard_acquire(int* g) { return !*reinterpret_cast<char*>(g); }
void __cxa_guard_release(int* g) { *reinterpret_cast<char*>(g) = 1; }
void __cxa_guard_abort(int*) {}
int  __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
void* __dso_handle = nullptr;
void* malloc(size_t n) { return bump(n); }
void  free(void*) {}
void* calloc(size_t a, size_t b) { void* p = bump(a * b); if (p) std::memset(p, 0, a * b); return p; }
int*  __errno() { static int e; return &e; }
typedef void (*Ctor)();
extern Ctor __init_array_start[], __init_array_end[];

void cmain()
{
    for (Ctor* c = __init_array_start; c < __init_array_end; ++c) (*c)();
    emu::set_allocator(bump, nofree);
    const games::Info* gi = games::find(S.id);
    if (!gi) { MB.a[0] = 1; call(8); return; }
    games::Machine* m = gi->make();
    if (!m) { MB.a[0] = 2; call(8); return; }
    MB.a[0] = uint32_t(g_used);
    call(7);                                            // machine made: its size
    m->init(reinterpret_cast<const uint8_t* const*>(S.reg), S.rate, false);
    if (S.demo >= 0) m->set_demo_sounds(S.demo != 0);
    const int16_t* in = reinterpret_cast<const int16_t*>(S.inputs);
    uint16_t* fb = reinterpret_cast<uint16_t*>(S.fb);
    for (uint32_t f = 0; f < S.frames; ++f) {
        m->set_inputs(uint16_t(in[f * 4]));
        games::Analog an;
        an.lx = in[f * 4 + 1];
        an.ly = in[f * 4 + 2];
        m->set_analog(an);
        call(1);
        const int n = m->run_frame(g_audio, 2048);
        MB.a[0] = reinterpret_cast<uint32_t>(g_audio);
        MB.a[1] = uint32_t(n);
        call(2);
        if (S.render) {
            call(4);
            for (int y = 0; y < gi->height; ++y) m->render_line(y, fb + y * gi->width);
            MB.a[0] = uint32_t(gi->width);
            MB.a[1] = uint32_t(gi->height);
            call(3);
        }
    }
    call(9);
}
}

void* operator new(size_t n) { return bump(n); }
void* operator new[](size_t n) { return bump(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return bump(n); }
void  operator delete(void*) noexcept {}
void  operator delete(void*, size_t) noexcept {}
void  operator delete[](void*) noexcept {}
namespace std { const nothrow_t nothrow{}; }
