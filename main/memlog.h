// Memory report for the Log (Tom, 2026-10-20: "Memory allocation has been
// the source of almost ALL of our problems ... Do the math"). The board's
// own numbers: free RAM after each start-up step, the free blocks when a
// game starts, and how much of each task's stack is really used - so the
// RAM budget (docs/MEMORY.md) is measured, not guessed.
#pragma once

#ifdef __cplusplus
namespace memlog {

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
