// ROM zips over USB, written for this project (MIT): the board's side of
// the web page's "Load ROMs" (web/roms/), on the serial port. Plain C++,
// tested on the PC against a fake flash.
//
// Lines are ASCII, ending in "\n"; every line of ours starts "CYDA " so the
// page can pick them out of the board's log output (other lines are
// ignored both ways). The page asks, the board answers:
//   CYDA HELLO                  -> CYDA HELLO <version> <free bytes> <files>
//   CYDA LIST                   -> CYDA FILE <name> <size> <crc> ... CYDA END
//   CYDA CLEAR                  -> CYDA OK            (forgets every stored zip)
//   CYDA PUT <name> <size> <crc hex>
//                               -> CYDA READY <chunk> or CYDA ERR <why>
//     then the file's bytes, raw, a chunk at a time; after each chunk
//                               -> CYDA ACK <bytes so far>
//     and after the last one    -> CYDA OK <name>  or  CYDA ERR <why>
//   CYDA DONE                   -> CYDA GAMES <id> <id> ...   (now playable)
//   CYDA LOG                    -> CYDA LOG <line> ... CYDA LOGEND <lines>
//                                  (the board's log as text, oldest first)
// A PUT whose bytes stop coming for kTimeoutMs is dropped (CYDA ERR timeout).
#pragma once

#include "rom_store.h"

#include <cstddef>
#include <cstdint>

namespace usb_import {

constexpr uint32_t kChunk = 2048;
constexpr uint32_t kTimeoutMs = 5000;
constexpr int      kMaxLogLines = 1000;

class Session {
public:
    // Sends one line (no newline; the caller adds it).
    using Send = void (*)(void* ctx, const char* line);
    // Fills `out` with the playable games' ids, space-separated.
    using GameList = void (*)(void* ctx, char* out, size_t cap);

    // Line i (0 = the first) of the board's log as LOG sends it; false
    // past the end.
    using LogLine = bool (*)(void* ctx, int i, char* out, size_t cap);

    void init(rom_store::Store& store, Send send, GameList games, void* ctx, const char* version);
    void set_log(LogLine log) { log_ = log; }

    // Bytes from the serial port.
    void feed(const uint8_t* data, size_t len, uint32_t now_ms);
    // Call now and then: drops a PUT whose bytes stopped coming.
    void tick(uint32_t now_ms);

    // A file is coming in (the caller may hold back other work meanwhile).
    bool receiving() const { return mode_ == Mode::Bytes; }
    // True once after each DONE (the caller rescans its game list).
    bool take_done() { const bool d = done_; done_ = false; return d; }

private:
    enum class Mode : uint8_t { Lines, Bytes };

    void line(char* text);
    void reply(const char* fmt, ...);
    void flush_chunk();

    rom_store::Store* store_ = nullptr;
    Send     send_ = nullptr;
    GameList games_ = nullptr;
    LogLine  log_ = nullptr;
    void*    ctx_ = nullptr;
    const char* version_ = "";

    Mode     mode_ = Mode::Lines;
    char     line_[96] = {};
    size_t   line_len_ = 0;
    bool     line_overflow_ = false;

    char     name_[rom_store::kNameMax + 1] = {};
    uint32_t size_ = 0;
    uint32_t got_ = 0;          // bytes of the file received
    uint8_t  chunk_[kChunk];
    uint32_t chunk_len_ = 0;
    uint32_t last_ms_ = 0;
    bool     done_ = false;
};

} // namespace usb_import
