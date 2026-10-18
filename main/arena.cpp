// The game area (see arena.h).
#include "arena.h"

#include <esp_heap_caps.h>

#include <algorithm>

namespace arena {
namespace {

// Each block starts with where the area's top was before it, so freeing
// the newest block can give its room back.
struct Head {
    uint32_t prev_top;
    uint32_t pad;
};
constexpr uint32_t kAlign = 8;
constexpr uint32_t kHead = sizeof(Head);

uint8_t* s_base = nullptr;
uint32_t s_size = 0, s_top = 0, s_last = 0, s_peak = 0;   // s_last: the newest block's head, + 1 (0 = none)

uint32_t align(uint32_t n) { return (n + kAlign - 1) & ~(kAlign - 1); }

}  // namespace

bool reserve(uint32_t keep_free)
{
    if (s_base) return true;
    constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t free_now = uint32_t(heap_caps_get_free_size(caps));
    const uint32_t largest = uint32_t(heap_caps_get_largest_free_block(caps));
    if (free_now <= keep_free) return false;
    uint32_t want = std::min(largest, free_now - keep_free) & ~(kAlign - 1);
    // The allocator's own bookkeeping takes a little of the block: step down
    // until one fits
    while (want >= 16 * 1024) {
        s_base = static_cast<uint8_t*>(heap_caps_malloc(want, caps));
        if (s_base) break;
        want -= 256;
    }
    if (!s_base) return false;
    s_size = want;
    s_top = s_last = 0;
    return true;
}

uint32_t renew(uint32_t keep_free)
{
    const uint32_t before = s_size;
    if (s_base) heap_caps_free(s_base);
    s_base = nullptr;
    s_size = s_top = s_last = 0;
    reserve(keep_free);
    return before;
}

void* alloc(size_t n)
{
    if (!s_base) return nullptr;
    const uint32_t need = kHead + align(uint32_t(n));
    if (n > s_size || need > s_size - s_top) return nullptr;
    Head* h = reinterpret_cast<Head*>(s_base + s_top);
    h->prev_top = s_top;
    h->pad = s_last;
    s_last = s_top + 1;
    s_top += need;
    s_peak = std::max(s_peak, s_top);
    return s_base + h->prev_top + kHead;
}

void free(void* p)
{
    if (!p || !s_base || !s_last) return;
    const Head* h = reinterpret_cast<const Head*>(s_base + (s_last - 1));
    if (static_cast<uint8_t*>(p) != s_base + h->prev_top + kHead) return;   // not the newest
    s_top = h->prev_top;
    s_last = h->pad;
}

void reset() { s_top = s_last = 0; }

uint32_t size() { return s_size; }
uint32_t used() { return s_top; }
uint32_t room() { return s_size - s_top > kHead ? s_size - s_top - kHead : 0; }
uint32_t peak() { return s_peak; }

}  // namespace arena
