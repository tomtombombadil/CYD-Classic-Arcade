// DAC streaming for audio_out.h. ESP-IDF's continuous DAC driver sends
// buffers by DMA at kRate; the task below blocks on each write, so it uses
// no CPU while the DMA plays.
#include "audio_out.h"

#include <driver/dac_continuous.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board_select.h"

namespace audio {
namespace {

constexpr const char* kTag = "audio";
constexpr int kChunk = 256;  // samples per write (~10.7 ms)

dac_continuous_handle_t s_dac = nullptr;
Fill s_fill = nullptr;
void* s_ctx = nullptr;
// 0..1024, from the volume: (v/100)^2, so 50 % = 256 = a game's samples
// / 256 onto the 8-bit DAC. v0.1-v0.5 used a quarter of that: about a
// fifth of CYD Classic Games' loudness at the same setting, and nothing
// heard on Tom's 2.8" ILI9341. Loud passages clip near 100 %.
volatile int s_gain = 256;

void audio_task(void*) {
    static int16_t pcm[kChunk];
    static uint8_t out[kChunk];
    for (;;) {
        s_fill(pcm, kChunk, s_ctx);
        const int g = s_gain;
        for (int i = 0; i < kChunk; i++) {
            int v = (pcm[i] * g) >> 16;
            if (v < -128) v = -128;
            if (v > 127) v = 127;
            out[i] = (uint8_t)(v + 128);
        }
        size_t done = 0;
        dac_continuous_write(s_dac, out, kChunk, &done, -1);
    }
}

}  // namespace

void set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_gain = 1024 * percent * percent / 10000;
}

bool start(Fill fill, void* ctx) {
    s_fill = fill;
    s_ctx = ctx;
#ifdef BOARD_PIN_AUDIO_EN
    // ESP32-32E boards: the amp is off unless this pin is low
    gpio_reset_pin((gpio_num_t)BOARD_PIN_AUDIO_EN);
    gpio_set_direction((gpio_num_t)BOARD_PIN_AUDIO_EN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)BOARD_PIN_AUDIO_EN, 0);
#endif
    static_assert(BOARD_PIN_SPEAKER == 26, "the speaker must be on DAC channel 2 (GPIO26)");
    dac_continuous_config_t cfg = {};
    cfg.chan_mask = DAC_CHANNEL_MASK_CH1;  // CH0 = GPIO25, CH1 = GPIO26
    cfg.desc_num = 4;
    cfg.buf_size = kChunk;
    cfg.freq_hz = kRate;
    cfg.offset = 0;
    cfg.clk_src = DAC_DIGI_CLK_SRC_DEFAULT;
    cfg.chan_mode = DAC_CHANNEL_MODE_SIMUL;
    esp_err_t err = dac_continuous_new_channels(&cfg, &s_dac);
    if (err == ESP_OK) err = dac_continuous_enable(s_dac);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "DAC start failed: %s", esp_err_to_name(err));
        return false;
    }
    set_volume(50);
    xTaskCreatePinnedToCore(audio_task, "audio", 3072, nullptr, 10, nullptr, 1);
    return true;
}

}  // namespace audio
