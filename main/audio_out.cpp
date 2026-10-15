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
//
// Or PWM (the menu's Sound Output setting): the speaker pin switched on and
// off 78,125 times a second, the share of "on" following the sound (10
// bits), the way CYD Classic Games drives it. Tom's 2.8" ILI9341 played
// nothing from the DAC (v0.5.0 - v0.6.1). A timer interrupt takes the next
// sample 24,000 times a second.
#include "audio_out.h"

#include <driver/dac_continuous.h>
#include <driver/gpio.h>
#include <driver/gptimer.h>
#include <driver/ledc.h>
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

// The ESP32-32E boards can switch their amp off (BOARD_PIN_AUDIO_EN, low =
// on). It's off after ~0.2 s with nothing to play, as in CYD Classic Games:
// left on, it picked up a faint low crackle at idle (Tom's 3.5").
constexpr int kQuiet = 64;            // |sample| below this counts as silence
constexpr int kAmpOffChunks = 20;     // ~0.2 s

void amp(bool on) {
#ifdef BOARD_PIN_AUDIO_EN
    static bool s_on = false;
    if (on == s_on) return;
    gpio_set_level((gpio_num_t)BOARD_PIN_AUDIO_EN, on ? 0 : 1);
    s_on = on;
#else
    (void)on;
#endif
}

// ---- PWM output ----
constexpr int kPwmFifo = 2048;        // samples, power of two (~85 ms)
uint16_t s_fifo[kPwmFifo];
volatile uint32_t s_head = 0, s_tail = 0;   // producer: audio task; consumer: the timer interrupt
bool s_pwm = false;
gptimer_handle_t s_timer = nullptr;

bool on_sample(gptimer_handle_t, const gptimer_alarm_event_data_t*, void*) {
    static uint32_t last = 0;
    const uint32_t t = s_tail;
    if (t != s_head) {
        last = s_fifo[t & (kPwmFifo - 1)];
        s_tail = t + 1;
    }
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, last);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
    return false;
}

bool start_pwm() {
    ledc_timer_config_t tc = {};
    tc.speed_mode = LEDC_HIGH_SPEED_MODE;     // the backlight has channel 7 / timer 3
    tc.duty_resolution = LEDC_TIMER_10_BIT;
    tc.timer_num = LEDC_TIMER_0;
    tc.freq_hz = 78125;                       // 80 MHz / 1024
    tc.clk_cfg = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&tc);
    if (err == ESP_OK) {
        ledc_channel_config_t cc = {};
        cc.gpio_num = BOARD_PIN_SPEAKER;
        cc.speed_mode = LEDC_HIGH_SPEED_MODE;
        cc.channel = LEDC_CHANNEL_0;
        cc.timer_sel = LEDC_TIMER_0;
        cc.duty = 0;
        cc.hpoint = 0;
        err = ledc_channel_config(&cc);
    }
    if (err == ESP_OK) {
        gptimer_config_t gc = {};
        gc.clk_src = GPTIMER_CLK_SRC_DEFAULT;
        gc.direction = GPTIMER_COUNT_UP;
        gc.resolution_hz = 8000000;
        err = gptimer_new_timer(&gc, &s_timer);
    }
    if (err == ESP_OK) {
        gptimer_event_callbacks_t cbs = {};
        cbs.on_alarm = on_sample;
        err = gptimer_register_event_callbacks(s_timer, &cbs, nullptr);
    }
    if (err == ESP_OK) {
        gptimer_alarm_config_t ac = {};
        ac.alarm_count = 333;                 // 24,024 Hz (8 MHz has no exact 24 kHz divider)
        ac.reload_count = 0;
        ac.flags.auto_reload_on_alarm = true;
        err = gptimer_set_alarm_action(s_timer, &ac);
    }
    if (err == ESP_OK) err = gptimer_enable(s_timer);
    if (err == ESP_OK) err = gptimer_start(s_timer);
    if (err != ESP_OK) ESP_LOGE(kTag, "PWM start failed: %s", esp_err_to_name(err));
    return err == ESP_OK;
}

// One chunk out by PWM. The resting level slides between 0 (silent: the
// pin stays low, as it does between CYD Classic Games' tones) and the
// middle (512) over ~5 ms, so starting and stopping don't click.
void pwm_out(const int16_t* pcm, int g, bool idle) {
    static int bias = 0;
    for (int i = 0; i < kChunk; i++) {
        while (s_head - s_tail >= uint32_t(kPwmFifo)) vTaskDelay(1);
        const int target = idle ? 0 : 512;
        if (bias < target) bias = bias + 4 > target ? target : bias + 4;
        if (bias > target) bias = bias - 4 < target ? target : bias - 4;
        int d = bias + ((pcm[i] * g) >> 14);
        if (d < 0) d = 0;
        if (d > 1023) d = 1023;
        s_fifo[s_head & (kPwmFifo - 1)] = uint16_t(d);
        s_head = s_head + 1;
    }
}

void audio_task(void*) {
    static int16_t pcm[kChunk];
    static uint8_t out[kChunk * kOver];
    int prev = 0;  // last game sample, in DAC steps x 256
    int err = 0;   // rounding error carried to the next step, x 256
    int quiet_chunks = kAmpOffChunks;
    for (;;) {
        s_fill(pcm, kChunk, s_ctx);
        bool quiet = true;
        for (int i = 0; i < kChunk && quiet; i++) quiet = pcm[i] > -kQuiet && pcm[i] < kQuiet;
        quiet_chunks = quiet ? quiet_chunks + 1 : 0;
        if (!quiet) amp(true);
        else if (quiet_chunks >= kAmpOffChunks) amp(false);
        const int g = s_gain;
        if (s_pwm) {
            pwm_out(pcm, g, quiet && quiet_chunks >= kAmpOffChunks);
            continue;
        }
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

bool start(Fill fill, void* ctx, bool pwm) {
    s_fill = fill;
    s_ctx = ctx;
    s_pwm = pwm;
#ifdef BOARD_PIN_AUDIO_EN
    // ESP32-32E boards: the amp is off unless this pin is low
    gpio_reset_pin((gpio_num_t)BOARD_PIN_AUDIO_EN);
    gpio_set_direction((gpio_num_t)BOARD_PIN_AUDIO_EN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)BOARD_PIN_AUDIO_EN, 1);   // off until there's sound
#endif
    if (pwm) {
        if (!start_pwm()) return false;
        set_volume(50);
        xTaskCreatePinnedToCore(audio_task, "audio", 3072, nullptr, 10, nullptr, 1);
        s_running = true;
        return true;
    }
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
