// The game area (see arena.h).
#include "arena.h"

#include <esp_heap_caps.h>

#include <algorithm>

namespace arena {
namespace {

// Each block starts with where its chunk's top was before it, so freeing
// the newest block of a chunk can give its room back.
struct Head {
    uint32_t prev_top;
    uint32_t prev_last;
};
constexpr uint32_t kAlign = 8;
constexpr uint32_t kHead = sizeof(Head);
constexpr uint32_t kMinChunk = 4 * 1024;    // smaller pieces aren't worth taking
constexpr uint32_t kCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

struct Chunk {
    uint8_t* base;
    uint32_t size, top, last;   // last: the newest block's head + 1 (0 = none)
    uint32_t room() const { return size - top > kHead ? size - top - kHead : 0; }
};
Chunk s_chunk[kMaxChunks];
int s_count = 0;
uint32_t s_peak = 0;

uint32_t align(uint32_t n) { return (n + kAlign - 1) & ~(kAlign - 1); }

}  // namespace

bool reserve(uint32_t keep_free)
{
    if (s_count) return true;
    // The biggest free block first, then the next biggest, while keep_free
    // stays free: the ESP32's RAM is several regions and Bluetooth's own
    // blocks split the biggest one (Tom's 3.2", v0.15.2: one block gave a
    // 32 KB area with much more free)
    while (s_count < kMaxChunks) {
        const uint32_t free_now = uint32_t(heap_caps_get_free_size(kCaps));
        const uint32_t largest = uint32_t(heap_caps_get_largest_free_block(kCaps));
        if (free_now <= keep_free + kMinChunk) break;
        uint32_t want = std::min(largest, free_now - keep_free) & ~(kAlign - 1);
        uint8_t* p = nullptr;
        // The allocator's own bookkeeping takes a little of the block: step
        // down until one fits
        while (want >= kMinChunk && !(p = static_cast<uint8_t*>(heap_caps_malloc(want, kCaps)))) want -= 256;
        if (!p) break;
        s_chunk[s_count++] = Chunk{p, want, 0, 0};
    }
    return s_count > 0;
}

void release()
{
    for (int i = 0; i < s_count; ++i) heap_caps_free(s_chunk[i].base);
    s_count = 0;
}

uint32_t renew(uint32_t keep_free)
{
    const uint32_t before = size();
    release();
    reserve(keep_free);
    return before;
}

void* alloc(size_t n)
{
    // The chunk with the least room that still takes it, so big pieces are
    // kept for big blocks (ROM regions come last)
    const uint32_t need = kHead + align(uint32_t(n));
    Chunk* c = nullptr;
    for (int i = 0; i < s_count; ++i) {
        Chunk& k = s_chunk[i];
        if (n <= k.size && need <= k.size - k.top && (!c || k.size - k.top < c->size - c->top)) c = &k;
    }
    if (!c) return nullptr;
    Head* h = reinterpret_cast<Head*>(c->base + c->top);
    h->prev_top = c->top;
    h->prev_last = c->last;
    c->last = c->top + 1;
    c->top += need;
    s_peak = std::max(s_peak, used());
    return c->base + h->prev_top + kHead;
}

void free(void* p)
{
    uint8_t* q = static_cast<uint8_t*>(p);
    for (int i = 0; q && i < s_count; ++i) {
        Chunk& c = s_chunk[i];
        if (q < c.base || q >= c.base + c.size) continue;
        if (!c.last) return;
        const Head* h = reinterpret_cast<const Head*>(c.base + (c.last - 1));
        if (q != c.base + h->prev_top + kHead) return;      // not the newest in its chunk
        c.top = h->prev_top;
        c.last = h->prev_last;
        return;
    }
}

void reset()
{
    for (int i = 0; i < s_count; ++i) s_chunk[i].top = s_chunk[i].last = 0;
}

uint32_t size()
{
    uint32_t n = 0;
    for (int i = 0; i < s_count; ++i) n += s_chunk[i].size;
    return n;
}

uint32_t used()
{
    uint32_t n = 0;
    for (int i = 0; i < s_count; ++i) n += s_chunk[i].top;
    return n;
}

uint32_t room()
{
    uint32_t n = 0;
    for (int i = 0; i < s_count; ++i) n = std::max(n, s_chunk[i].room());
    return n;
}

uint32_t room_total()
{
    uint32_t n = 0;
    for (int i = 0; i < s_count; ++i) n += s_chunk[i].room();
    return n;
}

int chunks() { return s_count; }
uint32_t chunk_size(int i) { return i >= 0 && i < s_count ? s_chunk[i].size : 0; }
uint32_t peak() { return s_peak; }

}  // namespace arena
