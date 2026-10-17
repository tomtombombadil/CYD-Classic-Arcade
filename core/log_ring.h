// A ring of text lines in a fixed buffer (the board's log, main/devlog.*),
// oldest lines dropped whole. Plain C++, no locking (the caller locks), so
// it is tested on the PC. The struct is plain data: it lives in RTC memory
// on the board and is checked with sane() after a reset.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace log_ring {

template <uint32_t N> struct Ring {
    static constexpr uint32_t kMagic = 0x4C4F4731;     // "LOG1"
    uint32_t magic;
    uint32_t head;      // where the next line goes
    uint32_t used;      // bytes in the ring: whole lines, each ending in a NUL
    uint32_t boots;     // start-ups counted (kept with the lines)
    char     buf[N];

    bool sane() const { return magic == kMagic && head < N && used <= N; }

    void reset(uint32_t keep_boots)
    {
        magic = kMagic;
        head = used = 0;
        boots = keep_boots;
    }

    char at(uint32_t back) const { return buf[(head + N - 1 - back) % N]; }   // back = 0: the newest byte

    void put(const char* text)
    {
        uint32_t len = uint32_t(std::strlen(text)) + 1;
        if (len > N) len = N;
        while (used + len > N) drop_oldest();
        for (uint32_t i = 0; i + 1 < len; ++i) buf[(head + i) % N] = text[i];
        buf[(head + len - 1) % N] = 0;
        head = (head + len) % N;
        used += len;
    }

    void drop_oldest()
    {
        const uint32_t start = (head + N - used) % N;
        uint32_t n = 0;
        while (n < used && buf[(start + n) % N] != 0) ++n;
        used -= n < used ? n + 1 : used;
    }

    int count() const
    {
        int n = 0;
        for (uint32_t i = 0; i < used; ++i) n += at(i) == 0;
        return n;
    }

    // Line `want`, newest first (0 = the latest); false past the end.
    bool line(int want, char* out, size_t cap) const
    {
        if (cap) out[0] = 0;
        uint32_t back = 0;     // at(back) is the NUL ending line k
        for (int k = 0; back < used; ++k) {
            uint32_t len = 0;
            while (back + 1 + len < used && at(back + 1 + len) != 0) ++len;
            if (k == want) {
                if (!cap) return true;
                const uint32_t n = len < cap - 1 ? len : uint32_t(cap - 1);
                for (uint32_t i = 0; i < n; ++i) out[i] = at(back + len - i);
                out[n] = 0;
                return true;
            }
            back += len + 1;
        }
        return false;
    }
};

}  // namespace log_ring
