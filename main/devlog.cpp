// The board's log (see devlog.h).
#include "devlog.h"

#include "core/log_ring.h"

#include <esp_attr.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace devlog {
namespace {

constexpr size_t kLineMax = 112;

RTC_NOINIT_ATTR log_ring::Ring<4096> s_ring;
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

}  // namespace

void init()
{
    const bool keep = s_ring.sane() && esp_reset_reason() != ESP_RST_POWERON;
    if (!keep) s_ring.reset(s_ring.sane() ? s_ring.boots : 0);
    ++s_ring.boots;
}

void add(const char* fmt, ...)
{
    char text[kLineMax];
    const int pre = std::snprintf(text, sizeof text, "[%u %.1fs] ", unsigned(s_ring.boots),
                                  double(esp_timer_get_time()) / 1e6);
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(text + pre, sizeof text - size_t(pre), fmt, ap);
    va_end(ap);
    ESP_LOGI("log", "%s", text);
    taskENTER_CRITICAL(&s_lock);
    if (s_ring.sane()) s_ring.put(text);
    taskEXIT_CRITICAL(&s_lock);
}

int count()
{
    taskENTER_CRITICAL(&s_lock);
    const int n = s_ring.sane() ? s_ring.count() : 0;
    taskEXIT_CRITICAL(&s_lock);
    return n;
}

bool line(int i, char* out, size_t cap)
{
    taskENTER_CRITICAL(&s_lock);
    const bool ok = s_ring.sane() && s_ring.line(i, out, cap);
    taskEXIT_CRITICAL(&s_lock);
    if (!ok && cap) out[0] = 0;
    return ok;
}

void clear()
{
    taskENTER_CRITICAL(&s_lock);
    s_ring.reset(s_ring.boots);
    taskEXIT_CRITICAL(&s_lock);
}

}  // namespace devlog

extern "C" void devlog_c(const char* text) { devlog::add("%s", text); }
