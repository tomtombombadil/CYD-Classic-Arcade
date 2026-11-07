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
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

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
// The panel is landscape; the menus are portrait: every rotation is turned
// a quarter more than on the portrait-native CYD panels (screen.cpp). Not
// LovyanGFX's offset_rotation: its RGB bus sizes the frame and its timing
// from the panel's rotated width / height at init.
#define BOARD_ROTATION_OFFSET   1
#define BOARD_REPORT            1    // chip / flash / PSRAM / I2C report in the log at every start

class LGFX_FreenoveRgb50 : public lgfx::LGFX_Device
{
    lgfx::Panel_RGB   _panel;
    lgfx::Bus_RGB     _bus;
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
            cfg.offset_rotation = 0;   // must stay 0 (see BOARD_ROTATION_OFFSET)
            _panel.config(cfg);
        }
        {
            auto cfg = _bus.config();
            cfg.panel = &_panel;
            cfg.pin_d0  = 8;    // B0
            cfg.pin_d1  = 3;    // B1
            cfg.pin_d2  = 46;   // B2
            cfg.pin_d3  = 9;    // B3
            cfg.pin_d4  = 1;    // B4
            cfg.pin_d5  = 5;    // G0
            cfg.pin_d6  = 6;    // G1
            cfg.pin_d7  = 7;    // G2
            cfg.pin_d8  = 15;   // G3
            cfg.pin_d9  = 16;   // G4
            cfg.pin_d10 = 4;    // G5
            cfg.pin_d11 = 45;   // R0
            cfg.pin_d12 = 48;   // R1
            cfg.pin_d13 = 47;   // R2
            cfg.pin_d14 = 21;   // R3
            cfg.pin_d15 = 14;   // R4
            cfg.pin_henable = 40;
            cfg.pin_vsync   = 41;
            cfg.pin_hsync   = 39;
            cfg.pin_pclk    = 42;
            cfg.freq_write  = BOARD_RGB_PCLK_HZ;
            cfg.hsync_polarity    = 0;
            cfg.hsync_front_porch = 4;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch  = 8;
            cfg.vsync_polarity    = 0;
            cfg.vsync_front_porch = 4;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch  = 8;
            // Data clocked out on PCLK's falling edge (Freenove). LovyanGFX
            // 1.2.31's Bus_RGB ignores pclk_active_neg and sets the LCD's
            // "out edge" bit from pclk_idle_high (ESP-IDF's own RGB driver
            // sets that bit from pclk_active_neg), so both are set.
            cfg.pclk_active_neg = 1;
            cfg.pclk_idle_high  = 1;
            cfg.de_idle_high    = 0;
            _bus.config(cfg);
        }
        _panel.setBus(&_bus);
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
    uint16_t* frame_buffer() { return reinterpret_cast<uint16_t*>(_bus.getDMABuffer(0)); }
};
