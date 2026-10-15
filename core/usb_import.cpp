// ROM zips over USB - see usb_import.h.
#include "usb_import.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace usb_import {

void Session::init(rom_store::Store& store, Send send, GameList games, void* ctx, const char* version)
{
    store_ = &store;
    send_ = send;
    games_ = games;
    ctx_ = ctx;
    version_ = version;
    mode_ = Mode::Lines;
    line_len_ = 0;
    line_overflow_ = false;
    done_ = false;
}

void Session::reply(const char* fmt, ...)
{
    char buf[640];      // the GAMES list is the longest
    std::memcpy(buf, "CYDA ", 5);
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf + 5, sizeof buf - 5, fmt, ap);
    va_end(ap);
    send_(ctx_, buf);
}

void Session::feed(const uint8_t* data, size_t len, uint32_t now_ms)
{
    size_t i = 0;
    while (i < len) {
        if (mode_ == Mode::Bytes) {
            last_ms_ = now_ms;
            const uint32_t want_file = size_ - got_;
            const uint32_t room = kChunk - chunk_len_;
            uint32_t n = uint32_t(len - i);
            if (n > room) n = room;
            if (n > want_file - chunk_len_) n = want_file - chunk_len_;
            std::memcpy(chunk_ + chunk_len_, data + i, n);
            chunk_len_ += n;
            i += n;
            if (chunk_len_ == kChunk || chunk_len_ == want_file) flush_chunk();
            continue;
        }
        const char c = char(data[i++]);
        if (c == '\r') continue;
        if (c == '\n') {
            line_[line_len_] = 0;
            if (!line_overflow_) line(line_);
            line_len_ = 0;
            line_overflow_ = false;
            if (mode_ == Mode::Bytes) last_ms_ = now_ms;
            continue;
        }
        if (line_len_ + 1 < sizeof line_) line_[line_len_++] = c;
        else line_overflow_ = true;
    }
}

void Session::flush_chunk()
{
    if (!store_->add(chunk_, chunk_len_)) {
        mode_ = Mode::Lines;
        store_->abort();
        reply("ERR write failed");
        return;
    }
    got_ += chunk_len_;
    chunk_len_ = 0;
    reply("ACK %u", unsigned(got_));
    if (got_ < size_) return;
    mode_ = Mode::Lines;
    if (store_->finish()) reply("OK %s", name_);
    else reply("ERR crc");
}

void Session::tick(uint32_t now_ms)
{
    if (mode_ == Mode::Bytes && now_ms - last_ms_ > kTimeoutMs) {
        mode_ = Mode::Lines;
        store_->abort();
        reply("ERR timeout");
    }
}

void Session::line(char* text)
{
    if (std::strncmp(text, "CYDA ", 5) != 0) return;
    char* cmd = text + 5;
    char* args = std::strchr(cmd, ' ');
    if (args) *args++ = 0;
    else args = cmd + std::strlen(cmd);

    if (std::strcmp(cmd, "HELLO") == 0) {
        int files = 0;
        for (int i = 0; i < store_->count(); ++i) files += store_->live(i);
        reply("HELLO %s %u %d", version_, unsigned(store_->free_bytes()), files);
    } else if (std::strcmp(cmd, "LIST") == 0) {
        for (int i = 0; i < store_->count(); ++i) {
            if (!store_->live(i)) continue;
            const rom_store::Entry& e = store_->entry(i);
            reply("FILE %s %u %08x", e.name, unsigned(e.size), unsigned(e.crc));
        }
        reply("END");
    } else if (std::strcmp(cmd, "CLEAR") == 0) {
        if (store_->clear()) reply("OK");
        else reply("ERR erase failed");
    } else if (std::strcmp(cmd, "PUT") == 0) {
        char name[64];
        unsigned size = 0, crc = 0;
        if (std::sscanf(args, "%63s %u %x", name, &size, &crc) != 3 || !rom_store::Store::valid_name(name)) {
            reply("ERR bad request");
            return;
        }
        if (size == 0 || size > store_->free_bytes()) {
            reply("ERR no room (%u bytes free)", unsigned(store_->free_bytes()));
            return;
        }
        if (!store_->begin(name, size, crc)) {
            reply("ERR store full");
            return;
        }
        std::strncpy(name_, name, rom_store::kNameMax);
        name_[rom_store::kNameMax] = 0;
        size_ = size;
        got_ = 0;
        chunk_len_ = 0;
        mode_ = Mode::Bytes;
        reply("READY %u", unsigned(kChunk));
    } else if (std::strcmp(cmd, "DONE") == 0) {
        done_ = true;
        char list[600] = {};    // ~40 games' ids
        if (games_) games_(ctx_, list, sizeof list);
        reply("GAMES %s", list);
    } else {
        reply("ERR unknown");
    }
}

} // namespace usb_import
