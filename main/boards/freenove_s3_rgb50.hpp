// Freenove 5.0" ESP32-S3 IPS display (FNK0115Q): 800x480 IPS panel on a
// 16-bit RGB parallel bus (no controller RAM: the picture is a frame buffer
// in PSRAM that the ESP32-S3's LCD peripheral streams out by DMA), GT911
// capacitive touch, NS4168 I2S amplifier, SD card on SPI, CH340C USB-serial.
//
// Pin facts from Freenove's FNK0115 schematic "5.0inch_ESP32-S3_IPS.pdf"
// and their example headers (their repository is CC BY-NC-SA: read for
// the pins and timings only, no code taken). The same pins as the common
// "ESP32-8048S050" 5" boards.
//   RGB bus  : DE 40, VSYNC 41, HSYNC 39, PCLK 42
//              R0-R4 45 48 47 21 14, G0-G5 5 6 7 15 16 4, B0-B4 8 3 46 9 1
//   Timing   : (IPS "Q" version) PCLK 13 MHz, data on the falling edge;
//              HSYNC / VSYNC polarity 0, front porch 4, pulse 4, back 8
//   Backlight: GPIO 2 (high = on)
//   Touch    : GT911, I2C SDA 19, SCL 20, INT 18, RST 38; reports 800x480
//   Audio    : NS4168 I2S amp: BCLK 0, LRCLK 18 (shared with touch INT!),
//              DATA 17; its CTRL on +5 V (always on). No codec, no MCLK.
//   SD card  : SPI - CS 10, MOSI 11, SCK 12, MISO 13
//   Module   : the schematic says ESP32-S3-WROOM-1 N8R2 (quad PSRAM); Tom's
//              boards are marked MCN16R8 (8 MB octal PSRAM, 16 MB flash) -
//              this build is for N16R8 (the board report in the log shows
//              which). The Hoysond 5" 800x480 ESP32-S3 board looks the same
//              (same module marking, same back): one env for both.
//
// Do not include directly - include boards/board_select.h.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/panel/Panel_FrameBufferBase.hpp>

#include <cstring>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>

#define BOARD_TOUCH_RESISTIVE   0    // capacitive: no calibration
#define BOARD_PIN_BOOT_BTN      0    // (also the amp's BCLK after start-up)
#define BOARD_SD_USABLE         1
#define BOARD_SD_LIBRARY        1    // play zips from the card (PSRAM; its own SPI bus)
#define BOARD_PIN_SD_CS         10
#define BOARD_PIN_SD_MOSI       11
#define BOARD_PIN_SD_SCK        12
#define BOARD_PIN_SD_MISO       13
#define BOARD_AUDIO_I2S         1    // I2S out...
#define BOARD_I2S_CODEC         0    // ...straight into an amp: no codec to set up, no MCLK
#define BOARD_PIN_I2S_BCLK      0
#define BOARD_PIN_I2S_WS        18
#define BOARD_PIN_I2S_DOUT      17
#define BOARD_PIN_I2C_SDA       19
#define BOARD_PIN_I2C_SCL       20
#define BOARD_I2C_PORT          0    // the touch's (LovyanGFX's I2C)
#define BOARD_PIN_TOUCH_INT     18
#define BOARD_PIN_TOUCH_RST     38
#define BOARD_RGB_PANEL         1    // frame buffer in PSRAM (screen::push writes it directly)
#define BOARD_NATIVE_W          800  // the panel's own (landscape) size
#define BOARD_NATIVE_H          480
#define BOARD_RGB_PCLK_HZ       13000000   // Freenove's IPS ("Q") timing
// Bounce buffers: 2 x 4 lines in internal RAM (12.5 KB), refilled every 4
// lines (250 us at 13 MHz) by the driver's interrupt on core 1
#define BOARD_RGB_BOUNCE_LINES  4
// The panel is landscape; the menus are portrait: every rotation is turned
// a quarter more than on the portrait-native CYD panels (screen.cpp). Not
// LovyanGFX's offset_rotation: its RGB bus sizes the frame and its timing
// from the panel's rotated width / height at init.
#define BOARD_ROTATION_OFFSET   1
#define BOARD_REPORT            1    // chip / flash / PSRAM / I2C report in the log at every start

// The panel through ESP-IDF's own RGB driver with BOUNCE BUFFERS. v0.26.0
// used LovyanGFX's Bus_RGB, whose DMA reads the frame straight from PSRAM:
// every redraw (the CPU writing PSRAM) starved it and the picture's left
// half dropped a few lines until the next frame (Tom, both boards, menus and
// games). Now the DMA reads two small buffers in internal RAM that the
// driver's interrupt refills from the frame buffer (through the data cache:
// what the CPU just wrote is seen without a write-back), so PSRAM traffic
// only has to keep up on average. The frame buffer is still one 800x480
// RGB565 block in PSRAM, in LovyanGFX's byte order (the data pins are
// wired with their bytes swapped, as LovyanGFX's bus did), so LovyanGFX
// draws into it as before and screen::push() writes it directly.
class Panel_RgbBounce : public lgfx::Panel_FrameBufferBase
{
    lgfx::Bus_NULL _null_bus;   // nothing to send: no stray bus call can fault
    esp_lcd_panel_handle_t _handle = nullptr;
    uint16_t* _fb = nullptr;

public:
    esp_lcd_rgb_panel_config_t rgb = {};   // filled in by the board

    Panel_RgbBounce()
    {
        _write_depth = lgfx::color_depth_t::rgb565_2Byte;
        _read_depth = lgfx::color_depth_t::rgb565_2Byte;
        setBus(&_null_bus);
    }

    bool init(bool use_reset) override
    {
        if (!Panel_FrameBufferBase::init(use_reset)) return false;   // light, rotation
        if (esp_lcd_new_rgb_panel(&rgb, &_handle) != ESP_OK) return false;
        if (esp_lcd_panel_reset(_handle) != ESP_OK || esp_lcd_panel_init(_handle) != ESP_OK) return false;
        void* fb = nullptr;
        if (esp_lcd_rgb_panel_get_frame_buffer(_handle, 1, &fb) != ESP_OK || !fb) return false;
        _fb = static_cast<uint16_t*>(fb);
        const int w = _cfg.panel_width, h = _cfg.panel_height;
        auto lines = static_cast<uint8_t**>(heap_caps_malloc(size_t(h) * sizeof(void*), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!lines) return false;
        for (int y = 0; y < h; ++y) lines[y] = reinterpret_cast<uint8_t*>(_fb + size_t(y) * w);
        _lines_buffer = lines;
        return true;
    }

    lgfx::color_depth_t setColorDepth(lgfx::color_depth_t) override { return _write_depth; }
    void writeCommand(uint32_t, uint_fast8_t) override {}
    void writeData(uint32_t, uint_fast8_t) override {}
    // No cache write-back: the bounce buffers are filled through the cache
    void display(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t) override
    {
        _range_mod.top = INT16_MAX;
        _range_mod.left = INT16_MAX;
        _range_mod.right = 0;
        _range_mod.bottom = 0;
    }

    uint16_t* frame_buffer() const { return _fb; }

    // The row table LovyanGFX draws through: rows found pointing anywhere
    // but their own place in the frame buffer are put back and counted (a
    // check for memory damage: a wrong row keeps its old pixels on screen
    // and LovyanGFX's writes for it land elsewhere)
    int repair_rows()
    {
        if (!_lines_buffer || !_fb) return 0;
        int bad = 0;
        const int w = _cfg.panel_width, h = _cfg.panel_height;
        for (int y = 0; y < h; ++y) {
            uint8_t* want = reinterpret_cast<uint8_t*>(_fb + size_t(y) * w);
            if (_lines_buffer[y] != want) { _lines_buffer[y] = want; ++bad; }
        }
        return bad;
    }
};

class LGFX_FreenoveRgb50 : public lgfx::LGFX_Device
{
    Panel_RgbBounce   _panel;
    lgfx::Light_PWM   _light;
    lgfx::Touch_GT911 _touch;

public:
    LGFX_FreenoveRgb50()
    {
        {
            auto cfg = _panel.config();
            cfg.memory_width  = BOARD_NATIVE_W;
            cfg.memory_height = BOARD_NATIVE_H;
            cfg.panel_width   = BOARD_NATIVE_W;
            cfg.panel_height  = BOARD_NATIVE_H;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;   // rotation is turned in screen.cpp (BOARD_ROTATION_OFFSET)
            _panel.config(cfg);
        }
        {
            esp_lcd_rgb_panel_config_t& c = _panel.rgb;
            c.clk_src = LCD_CLK_SRC_DEFAULT;
            c.timings.pclk_hz = BOARD_RGB_PCLK_HZ;
            c.timings.h_res = BOARD_NATIVE_W;
            c.timings.v_res = BOARD_NATIVE_H;
            c.timings.hsync_pulse_width = 4;
            c.timings.hsync_back_porch = 8;
            c.timings.hsync_front_porch = 4;
            c.timings.vsync_pulse_width = 4;
            c.timings.vsync_back_porch = 8;
            c.timings.vsync_front_porch = 4;
            // As v0.26.0's LovyanGFX setup, which showed a correct picture:
            // H / V sync idle low, data out on PCLK's falling edge, PCLK
            // idle low, DE idle low
            c.timings.flags.hsync_idle_low = 1;
            c.timings.flags.vsync_idle_low = 1;
            c.timings.flags.pclk_active_neg = 1;
            c.data_width = 16;
            c.bits_per_pixel = 16;
            c.num_fbs = 1;
            c.bounce_buffer_size_px = BOARD_NATIVE_W * BOARD_RGB_BOUNCE_LINES;
            c.dma_burst_size = 64;
            c.hsync_gpio_num = 39;
            c.vsync_gpio_num = 41;
            c.de_gpio_num = 40;
            c.pclk_gpio_num = 42;
            c.disp_gpio_num = -1;
            // B0-B4, G0-G5, R0-R4; data line i on signal i ^ 8 - the frame
            // buffer holds RGB565 byte-swapped (LovyanGFX's order)
            static const int kData[16] = {8, 3, 46, 9, 1, 5, 6, 7, 15, 16, 4, 45, 48, 47, 21, 14};
            for (int i = 0; i < 16; ++i) c.data_gpio_nums[i ^ 8] = kData[i];
            c.flags.fb_in_psram = 1;
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl      = 2;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {   // GT911: both addresses are tried (0x5D first - screen.cpp holds
            // INT low through its reset); INT is never read: it is the
            // amp's LRCLK too
            auto cfg = _touch.config();
            cfg.i2c_port   = BOARD_I2C_PORT;
            cfg.i2c_addr   = 0x5D;
            cfg.pin_sda    = BOARD_PIN_I2C_SDA;
            cfg.pin_scl    = BOARD_PIN_I2C_SCL;
            cfg.pin_int    = -1;
            cfg.pin_rst    = BOARD_PIN_TOUCH_RST;
            cfg.freq       = 400000;
            cfg.x_min      = 0;
            cfg.x_max      = BOARD_NATIVE_W - 1;
            cfg.y_min      = 0;
            cfg.y_max      = BOARD_NATIVE_H - 1;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }

    // The frame buffer (BOARD_NATIVE_W x BOARD_NATIVE_H, byte-swapped RGB565
    // as the SPI panels take it), nullptr before init / without PSRAM
    uint16_t* frame_buffer() { return _panel.frame_buffer(); }
    int repair_rows() { return _panel.repair_rows(); }
};
