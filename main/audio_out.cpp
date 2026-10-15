// DAC streaming for audio_out.h. ESP-IDF's continuous DAC driver sends
// buffers by DMA; the task below blocks on each write, so it uses no CPU
// while the DMA plays.
//
// The DAC has only 256 steps, and the games' sound sits well below full
// scale (Pac-Man's music averages ~2,300 of 32,767), so plain conversion
// left it on a handful of steps - "distorted and noisy" (Tom, v0.5.0:
// Donkey Kong, Pac-Man). So the DAC runs at 4x the game rate (96 kHz):
// each game sample is joined to the next by a straight line (no 24 kHz
// staircase, whose mirror images landed in the treble), and the rounding
// error of each step is carried into the next (first-order noise shaping),
// which moves most of the rounding noise above hearing.
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
constexpr int kChunk = 256;  // game samples per write (~10.7 ms)
constexpr int kOver = 4;     // DAC samples per game sample

dac_continuous_handle_t s_dac = nullptr;
Fill s_fill = nullptr;
void* s_ctx = nullptr;
// 0..400, from the volume, linear: 100 % puts the loudest game peaks
// (~21,000) just under the DAC's full scale, so nothing clips at any
// setting. 50 % = 200, about 3x v0.5.0's level (v0.5.1 tried 4x with a
// square law, which clipped above ~65 %).
constexpr int kGainMax = 400;
volatile int s_gain = kGainMax / 2;
bool s_running = false;

void audio_task(void*) {
    static int16_t pcm[kChunk];
    static uint8_t out[kChunk * kOver];
    int prev = 0;  // last game sample, in DAC steps x 256
    int err = 0;   // rounding error carried to the next step, x 256
    for (;;) {
        s_fill(pcm, kChunk, s_ctx);
        const int g = s_gain;
        uint8_t* o = out;
        for (int i = 0; i < kChunk; i++) {
            const int cur = (pcm[i] * g) >> 8;   // DAC steps x 256
            const int step = (cur - prev) / kOver;
            int x = prev;
            for (int k = 0; k < kOver; k++) {
                x += step;
                const int u = x - err;
                int q = (u + 128 + (128 << 8)) >> 8;   // round to a step, 0..255
                if (q < 0) q = 0;
                if (q > 255) q = 255;
                err = ((q - 128) << 8) - u;
                if (err > 256) err = 256;            // after clipping: don't wind up
                if (err < -256) err = -256;
                *o++ = (uint8_t)q;
            }
            prev = cur;
        }
        size_t done = 0;
        dac_continuous_write(s_dac, out, sizeof out, &done, -1);
    }
}

}  // namespace

void set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_gain = kGainMax * percent / 100;
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
    cfg.buf_size = kChunk * kOver;
    cfg.freq_hz = kRate * kOver;
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
    s_running = true;
    return true;
}

bool running() { return s_running; }

}  // namespace audio
