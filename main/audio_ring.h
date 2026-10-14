// A single-producer, single-consumer ring of audio samples between the game
// loop (fills a frame at a time) and the audio task (drains at 24 kHz).
// Its fill level also paces the game: the loop waits while it's full.
#pragma once

#include <atomic>
#include <cstdint>

class AudioRing {
public:
    static constexpr uint32_t kSize = 4096;     // power of two

    uint32_t count() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }
    uint32_t space() const { return kSize - count(); }

    // Producer: as many as fit; returns how many went in.
    uint32_t push(const int16_t* in, uint32_t n)
    {
        const uint32_t room = space();
        if (n > room) n = room;
        uint32_t h = head_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < n; ++i) buf_[(h + i) & (kSize - 1)] = in[i];
        head_.store(h + n, std::memory_order_release);
        return n;
    }

    // Consumer: fills `out`, silence where the ring runs dry.
    void pop(int16_t* out, uint32_t n)
    {
        uint32_t t = tail_.load(std::memory_order_relaxed);
        const uint32_t have = head_.load(std::memory_order_acquire) - t;
        const uint32_t take = n < have ? n : have;
        for (uint32_t i = 0; i < take; ++i) out[i] = buf_[(t + i) & (kSize - 1)];
        for (uint32_t i = take; i < n; ++i) out[i] = 0;
        tail_.store(t + take, std::memory_order_release);
    }

    void clear() { tail_.store(head_.load()); }

private:
    int16_t buf_[kSize] = {};
    std::atomic<uint32_t> head_{0}, tail_{0};
};
