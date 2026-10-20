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
//
// Boards with an audio codec (BOARD_AUDIO_I2S: the Freenove ESP32-S3
// boards' ES8311) get 16-bit samples by I2S at the game rate instead; the
// codec does the rest. No DAC / PWM choice there.
#include "audio_out.h"

#include "board_select.h"
#include "memlog.h"

#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if BOARD_AUDIO_I2S
#include <driver/i2s_std.h>
#else
#include <driver/dac_continuous.h>
#include <driver/gptimer.h>
#include <driver/ledc.h>
#endif

namespace audio {
namespace {

constexpr const char* kTag = "audio";
constexpr int kChunk = 256;  // game samples per write (~10.7 ms)
constexpr int kOver = 4;     // DAC samples per game sample

#if !BOARD_AUDIO_I2S
dac_continuous_handle_t s_dac = nullptr;
#endif
Fill s_fill = nullptr;
void* s_ctx = nullptr;
// 0..400, from the volume, linear: 100 % puts the loudest game peaks
// (~21,000) just under the DAC's full scale, so nothing clips at any
// setting. 50 % = 200, about 3x v0.5.0's level (v0.5.1 tried 4x with a
// square law, which clipped above ~65 %).
constexpr int kGainMax = 400;
volatile int s_gain = kGainMax / 2;
bool s_running = false;
bool s_pwm = false;     // the Sound Output setting (DAC boards)

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

#if BOARD_AUDIO_I2S
// ---- I2S into the ES8311 codec ----
// Register values from the ES8311 user guide (the same ones Espressif's
// Apache-2.0 driver writes; no code taken): slave mode, MCLK from its pin
// at 256 x the sample rate, I2S 16-bit in, DAC and output stage powered.
i2s_chan_handle_t s_tx = nullptr;

bool codec_init() {
    // The touch controller shares this bus: LovyanGFX's I2C driver owns it
    // (the display started first), so the codec is written through it too
    auto res = lgfx::i2c::init(BOARD_I2C_PORT, BOARD_PIN_I2C_SDA, BOARD_PIN_I2C_SCL);
    bool ok = res.has_value();
    struct Reg { uint8_t reg, val; uint8_t wait_ms; };
    static const Reg kInit[] = {
        {0x00, 0x1F, 20},   // reset
        {0x00, 0x00, 0},
        {0x00, 0x80, 0},    // power on, slave
        {0x01, 0x3F, 0},    // every clock on, MCLK from the MCLK pin
        {0x02, 0x00, 0},    // pre-divider 1, multiplier 1 (MCLK = 256 fs)
        {0x03, 0x10, 0},    // single speed, ADC oversampling
        {0x04, 0x10, 0},    // DAC oversampling
        {0x05, 0x00, 0},    // ADC / DAC dividers 1
        {0x06, 0x03, 0},    // BCLK divider (unused as slave), not inverted
        {0x07, 0x00, 0},    // LRCK divider 256 (unused as slave)
        {0x08, 0xFF, 0},
        {0x09, 0x0C, 0},    // serial in: I2S, 16 bit
        {0x0A, 0x0C, 0},    // serial out: I2S, 16 bit
        {0x0D, 0x01, 0},    // analog circuits up
        {0x0E, 0x02, 0},    // PGA / ADC modulator up
        {0x12, 0x00, 0},    // DAC up
        {0x13, 0x10, 0},    // output driver on
        {0x1C, 0x6A, 0},    // ADC equaliser bypassed, DC offset cancelled
        {0x37, 0x08, 0},    // DAC equaliser bypassed
        {0x31, 0x00, 0},    // DAC not muted
        {0x32, 0xBF, 0},    // DAC volume 0 dB (the volume setting is ours)
    };
    for (const Reg& r : kInit) {
        if (!ok) break;
        ok = lgfx::i2c::writeRegister8(BOARD_I2C_PORT, BOARD_I2C_CODEC_ADDR, r.reg, r.val, 0, 100000).has_value();
        if (r.wait_ms) vTaskDelay(pdMS_TO_TICKS(r.wait_ms));
    }
    if (!ok) ESP_LOGE(kTag, "ES8311 codec not set up (no answer on I2C)");
    return ok;
}

bool start_i2s() {
    i2s_chan_config_t cc = {};
    cc.id = I2S_NUM_0;
    cc.role = I2S_ROLE_MASTER;
    cc.dma_desc_num = 4;
    cc.dma_frame_num = kChunk;
    cc.auto_clear = true;               // silence, not a repeated buffer, if late
    esp_err_t err = i2s_new_channel(&cc, &s_tx, nullptr);
    if (err == ESP_OK) {
        i2s_std_config_t sc = {};
        sc.clk_cfg.sample_rate_hz = kRate;
        sc.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
        sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
        sc.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
        sc.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
        sc.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
        sc.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
        sc.slot_cfg.ws_width = 16;
        sc.slot_cfg.ws_pol = false;
        sc.slot_cfg.bit_shift = true;   // Philips I2S
#if SOC_I2S_HW_VERSION_2
        sc.slot_cfg.left_align = true;
        sc.slot_cfg.big_endian = false;
        sc.slot_cfg.bit_order_lsb = false;
#endif
        sc.gpio_cfg.mclk = gpio_num_t(BOARD_PIN_I2S_MCLK);
        sc.gpio_cfg.bclk = gpio_num_t(BOARD_PIN_I2S_BCLK);
        sc.gpio_cfg.ws = gpio_num_t(BOARD_PIN_I2S_WS);
        sc.gpio_cfg.dout = gpio_num_t(BOARD_PIN_I2S_DOUT);
        sc.gpio_cfg.din = I2S_GPIO_UNUSED;
        err = i2s_channel_init_std_mode(s_tx, &sc);
    }
    if (err == ESP_OK) err = i2s_channel_enable(s_tx);   // MCLK runs before the codec is set up
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "I2S start failed: %s", esp_err_to_name(err));
        return false;
    }
    return codec_init();
}

void i2s_out(const int16_t* pcm, int g) {
    static int16_t out[kChunk];
    for (int i = 0; i < kChunk; i++) {
        int v = (pcm[i] * g) >> 8;      // same scale as the DAC path: 100 % = peaks near full scale
        out[i] = int16_t(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
    }
    size_t done = 0;
    i2s_channel_write(s_tx, out, sizeof out, &done, portMAX_DELAY);
}

#else
// ---- PWM output ----
constexpr int kPwmFifo = 2048;        // samples, power of two (~85 ms)
uint16_t* s_fifo = nullptr;           // 4 KB, taken only when PWM is used (most boards use the DAC)
volatile uint32_t s_head = 0, s_tail = 0;   // producer: audio task; consumer: the timer interrupt
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
    if (!s_fifo) s_fifo = static_cast<uint16_t*>(heap_caps_calloc(kPwmFifo, sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!s_fifo) {
        ESP_LOGE(kTag, "PWM start failed: no RAM for its buffer");
        return false;
    }
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

#endif  // BOARD_AUDIO_I2S

#if !BOARD_AUDIO_I2S
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

#endif  // !BOARD_AUDIO_I2S

void audio_task(void*) {
    static int16_t pcm[kChunk];
#if !BOARD_AUDIO_I2S
    static uint8_t out[kChunk * kOver];
#endif
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
#if BOARD_AUDIO_I2S
        i2s_out(pcm, g);
        (void)prev;
        (void)err;
    }
#else
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
#endif
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
#if BOARD_AUDIO_I2S
    (void)pwm;
    if (!start_i2s()) return false;
    set_volume(50);
    xTaskCreatePinnedToCore(audio_task, "audio", memlog::kStackAudio, nullptr, 10, nullptr, 1);
    s_running = true;
    return true;
#else
    if (pwm) {
        if (!start_pwm()) return false;
        set_volume(50);
        xTaskCreatePinnedToCore(audio_task, "audio", memlog::kStackAudio, nullptr, 10, nullptr, 1);
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
    xTaskCreatePinnedToCore(audio_task, "audio", memlog::kStackAudio, nullptr, 10, nullptr, 1);
    s_running = true;
    return true;
#endif
}

bool running() { return s_running; }

bool has_output_choice() { return !BOARD_AUDIO_I2S; }

}  // namespace audio
