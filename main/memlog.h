// Memory report for the Log (Tom, 2026-10-20: "Memory allocation has been
// the source of almost ALL of our problems ... Do the math"). The board's
// own numbers: free RAM after each start-up step, the free blocks when a
// game starts, and how much of each task's stack is really used - so the
// RAM budget (docs/MEMORY.md) is measured, not guessed.
#pragma once

#include <sdkconfig.h>

#ifdef __cplusplus
#include <cstdint>

namespace memlog {

// Task stacks (bytes). ESP32: cut to the most measured over all 16 games
// and the menus on Tom's 3.5" (v0.15.7-v0.16.1: ui 5.6 KB, draw 1.2,
// audio 0.7, usb 0.7) plus ~40 %, for RAM the games need (docs/MEMORY.md).
// The S3 keeps the old sizes: RAM to spare, and its I2S sound and Star
// Wars drawn on core 0 weren't measured.
#if CONFIG_IDF_TARGET_ESP32
constexpr uint32_t kStackUi = 9 * 1024, kStackDraw = 3 * 1024, kStackAudio = 2 * 1024;
#else
constexpr uint32_t kStackUi = 12 * 1024, kStackDraw = 5 * 1024, kStackAudio = 3 * 1024;
#endif
constexpr uint32_t kStackUsb = 6 * 1024;     // only while USB Transfer is open

// "Mem <what>: 70.3 KB free, block 31.2 KB"
void stage(const char* what);
// The biggest free blocks of internal RAM: "Free blocks: 31.2 24.0 8.1 ..."
void blocks(const char* what);
// Each known task's stack: size and the least it has had free
void stacks();

}  // namespace memlog
extern "C" {
#endif

void memlog_stage_c(const char* what);

#ifdef __cplusplus
}
#endif
