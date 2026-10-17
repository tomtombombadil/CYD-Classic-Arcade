// Crash note (see crash_note.h).
#include "crash_note.h"

#include "devlog.h"

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_private/panic_internal.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if defined(__XTENSA__)
#include <esp_cpu_utils.h>
#include <esp_debug_helpers.h>
#include <xtensa_context.h>
#endif

#include <cstdio>
#include <cstring>

namespace crash_note {
namespace {

constexpr uint32_t kMagic = 0x43524E31;     // "CRN1": a step is noted
constexpr uint32_t kPanic = 0x50414E31;     // "PAN1": the panic handler ran
constexpr int kTrace = 8;

struct Note {
    uint32_t magic;
    char     game[24];
    char     what[32];
    uint32_t free_kb, block_kb;
    // filled by the panic handler
    uint32_t panic;
    char     reason[72];     // the exception, or abort()'s / an assert's text
    char     task[16];
    uint32_t addr;
    uint32_t core;
    uint32_t trace[kTrace];
    uint32_t n_trace;
};

RTC_NOINIT_ATTR Note s_note;

void copy(char* dst, size_t cap, const char* src)
{
    std::strncpy(dst, src ? src : "", cap - 1);
    dst[cap - 1] = 0;
}

}  // namespace

void step(const char* game, const char* what)
{
    copy(s_note.game, sizeof s_note.game, game);
    copy(s_note.what, sizeof s_note.what, what);
    s_note.free_kb = uint32_t(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024);
    s_note.block_kb = uint32_t(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024);
    s_note.magic = kMagic;
}

void clear() { s_note.magic = 0; }

bool last(char* out, size_t cap)
{
    const esp_reset_reason_t r = esp_reset_reason();
    const char* kind = r == ESP_RST_PANIC     ? "crashed"
                       : r == ESP_RST_INT_WDT ? "hung (interrupt watchdog)"
                       : r == ESP_RST_TASK_WDT ? "hung (task watchdog)"
                       : r == ESP_RST_WDT     ? "hung (watchdog)"
                       : r == ESP_RST_BROWNOUT ? "lost power (brownout)"
                                               : nullptr;
    const bool noted = s_note.magic == kMagic;
    const bool panicked = s_note.panic == kPanic;
    s_note.magic = 0;
    s_note.panic = 0;
    if (!kind) return false;
    s_note.game[sizeof s_note.game - 1] = 0;
    s_note.what[sizeof s_note.what - 1] = 0;
    s_note.reason[sizeof s_note.reason - 1] = 0;
    s_note.task[sizeof s_note.task - 1] = 0;
    if (noted)
        std::snprintf(out, cap, "%s %s while %s (%u KB free, block %u KB).", s_note.game, kind, s_note.what,
                      unsigned(s_note.free_kb), unsigned(s_note.block_kb));
    else
        std::snprintf(out, cap, "The board %s.", kind);
    // The whole story into the log (Settings > Log); the backtrace's
    // addresses decode with the build's firmware.elf (CI artifact)
    devlog::add("RESET: %s", out);
    if (panicked) {
        devlog::add("Crash: %s (task %s, core %u, at %08x)", s_note.reason[0] ? s_note.reason : "?", s_note.task,
                    unsigned(s_note.core), unsigned(s_note.addr));
        char bt[kTrace * 11 + 1] = "";
        size_t used = 0;
        for (uint32_t i = 0; i < s_note.n_trace && i < uint32_t(kTrace); ++i)
            used += size_t(std::snprintf(bt + used, sizeof bt - used, " %08x", unsigned(s_note.trace[i])));
        if (used) devlog::add("Backtrace:%s", bt);
    }
    return true;
}

namespace {
char s_start[160];
bool s_have_start = false;
}  // namespace

void at_start() { s_have_start = last(s_start, sizeof s_start); }
const char* start_message() { return s_have_start ? s_start : nullptr; }

// Called by ESP-IDF's panic handler (linked with --wrap=esp_panic_handler):
// keeps what the crash was in RTC memory, then the normal handler goes on
// (prints it on the serial port and resets).
void on_panic(const void* p)
{
    const panic_info_t* info = static_cast<const panic_info_t*>(p);
    s_note.panic = kPanic;
    s_note.core = uint32_t(info->core);
    s_note.addr = uint32_t(reinterpret_cast<uintptr_t>(info->addr));
    if (g_panic_abort && g_panic_abort_details) copy(s_note.reason, sizeof s_note.reason, g_panic_abort_details);
    else if (info->reason && info->description) {
        std::snprintf(s_note.reason, sizeof s_note.reason, "%s: %s", info->reason, info->description);
    } else {
        copy(s_note.reason, sizeof s_note.reason, info->reason ? info->reason : info->description);
    }
    const char* t = pcTaskGetName(nullptr);
    copy(s_note.task, sizeof s_note.task, t ? t : "?");
    s_note.n_trace = 0;
#if defined(__XTENSA__)
    if (info->frame) {
        const XtExcFrame* f = static_cast<const XtExcFrame*>(info->frame);
        esp_backtrace_frame_t fr = {};
        fr.pc = uint32_t(f->pc);
        fr.sp = uint32_t(f->a1);
        fr.next_pc = uint32_t(f->a0);
        fr.exc_frame = f;
        s_note.trace[s_note.n_trace++] = esp_cpu_process_stack_pc(fr.pc);
        while (s_note.n_trace < uint32_t(kTrace) && fr.next_pc && esp_backtrace_get_next_frame(&fr))
            s_note.trace[s_note.n_trace++] = esp_cpu_process_stack_pc(fr.pc);
    }
#endif
}

}  // namespace crash_note

extern "C" void __real_esp_panic_handler(panic_info_t* info);
extern "C" void __wrap_esp_panic_handler(panic_info_t* info)
{
    crash_note::on_panic(info);
    __real_esp_panic_handler(info);
}
