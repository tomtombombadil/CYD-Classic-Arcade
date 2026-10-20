// Packing the board's log into a QR code (Settings > Send Log). Plain
// C++, host-tested (tools/host_tests/core_tests.cpp). From the sister
// project CYD Classic Games (same owner, MIT), with the match tables passed
// in (the board takes them from the game area; nothing here allocates).
//
// The log is compressed with raw DEFLATE (RFC 1951; fixed Huffman codes and
// a 4 KB window - logs repeat a lot, ~5x smaller) and written in "base43":
// QR's alphanumeric characters minus space and '%' (both awkward in a URL),
// 2 bytes -> 3 characters. Alphanumeric QR mode stores 5.5 bits per
// character, so this packs ~97 % as densely as raw bytes while staying
// plain URL text. The web page (web/l/index.html) reverses it with the
// browser's own DecompressionStream("deflate-raw").
#pragma once

#include <cstddef>
#include <cstdint>

namespace logpack {

// The QR code is this URL followed by the packed log
constexpr const char* kUrl = "https://tomtombombadil.github.io/CYD-Classic-Arcade/l/#";
constexpr const char* kAlphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$*+-./:";   // 43
constexpr uint8_t kFormat = 1;          // first byte of the packed data

// The match tables deflate() works in (16 KB)
constexpr size_t kTablesBytes = (size_t(1) << 12) * 2 + (size_t(1) << 12) * 2;

// Raw DEFLATE of `in`, using `tables` (kTablesBytes, 2-byte aligned).
// Returns the bytes written, or 0 if `cap` is too small.
size_t deflate(const uint8_t* in, size_t n, uint8_t* out, size_t cap, void* tables);

// Base43 text of `in` (NUL-terminated). Needs cap >= n * 3 / 2 + 2.
// Returns the length, 0 if cap is too small.
size_t base43(const uint8_t* in, size_t n, char* out, size_t cap);

// The whole packet: kFormat, then deflate(text), as base43. Returns the
// length of `out` or 0. `work` must hold n + 64 bytes.
size_t pack(const char* text, size_t n, uint8_t* work, size_t work_cap, char* out, size_t cap, void* tables);

} // namespace logpack
