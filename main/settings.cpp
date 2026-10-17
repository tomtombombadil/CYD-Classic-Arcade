// NVS settings (see settings.h).
#include "settings.h"

#include <nvs.h>

namespace settings {
namespace {

Values s_values;
constexpr const char* kNs = "arcade";

}  // namespace

void load()
{
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v;
    if (nvs_get_u8(h, "swap_ab", &v) == ESP_OK) s_values.swap_ab = v != 0;
    if (nvs_get_u8(h, "volume", &v) == ESP_OK && v <= 100) s_values.volume = v;
    if (nvs_get_u8(h, "snd_pwm", &v) == ESP_OK) s_values.sound_pwm = v != 0;
    if (nvs_get_u8(h, "rot180", &v) == ESP_OK) s_values.rotate180 = v != 0;
    if (nvs_get_u8(h, "demo_snd", &v) == ESP_OK) s_values.demo_sounds = v != 0;
    else if (nvs_get_u8(h, "usb_top", &v) == ESP_OK) s_values.rotate180 = v == 0;   // v0.3-v0.5's "USB Port"
    nvs_close(h);
}

void save()
{
    nvs_handle_t h;
    if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "swap_ab", s_values.swap_ab ? 1 : 0);
    nvs_set_u8(h, "volume", s_values.volume);
    nvs_set_u8(h, "snd_pwm", s_values.sound_pwm ? 1 : 0);
    nvs_set_u8(h, "rot180", s_values.rotate180 ? 1 : 0);
    nvs_set_u8(h, "demo_snd", s_values.demo_sounds ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

Values& get() { return s_values; }

}  // namespace settings
