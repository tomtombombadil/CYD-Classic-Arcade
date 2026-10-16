// Freenove "ESP32-S3 Display" boards (FNK0104B 2.8" ILI9341, FNK0104S 4.0"
// ST7796), both with a capacitive touch screen. ESP32-S3R8: 8 MB octal PSRAM,
// native USB (GPIO19 / 20 to the USB-C port - no USB-serial chip).
//
// Pin facts from Freenove's schematics and sample setups (their repository
// is CC BY-NC-SA: read for the pins only, no code taken):
//   Display on SPI2 : SCLK 12, MOSI 11, MISO 13 (2.8" only), CS 10, DC 46,
//                     RST = EN; backlight 45 (high = on); inversion on
//   Touch FT6336    : I2C SDA 16, SCL 15, INT 17, RST 18, address 0x38; it
//                     reports in the panel's own portrait coordinates
//                     (Freenove's sample maps it that way)
//   Audio ES8311    : I2C SDA 16, SCL 15 (address 0x18); I2S MCLK 4, BCLK 5,
//                     WS 7, DOUT 8, DIN 6; speaker amp enable 1 (low = on)
//   SD card (SDMMC) : CLK 38, CMD 40, D0 39, D1 41, D2 48, D3 47
//   WS2812 LED 42, BOOT button 0, battery ADC 9
//
// Do not include directly - include boards/board_select.h.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define BOARD_TOUCH_RESISTIVE   0    // capacitive: no calibration
#define BOARD_PIN_BOOT_BTN      0
#define BOARD_SD_USABLE         1
#define BOARD_SD_SDMMC          1    // 4-bit SDMMC, not SPI
#define BOARD_PIN_SD_CLK        38
#define BOARD_PIN_SD_CMD        40
#define BOARD_PIN_SD_D0         39
#define BOARD_PIN_SD_D1         41
#define BOARD_PIN_SD_D2         48
#define BOARD_PIN_SD_D3         47
#define BOARD_AUDIO_I2S         1    // ES8311 codec, not the ESP32 DAC
#define BOARD_PIN_I2S_MCLK      4
#define BOARD_PIN_I2S_BCLK      5
#define BOARD_PIN_I2S_WS        7
#define BOARD_PIN_I2S_DOUT      8
#define BOARD_PIN_I2C_SDA       16
#define BOARD_PIN_I2C_SCL       15
#define BOARD_I2C_PORT          0    // shared by touch and the codec (LovyanGFX's I2C)
#define BOARD_I2C_CODEC_ADDR    0x18
#define BOARD_PIN_AUDIO_EN      1    // low = amplifier on
#define BOARD_PIN_BATTERY_ADC   9
#define BOARD_USB_SERIAL_JTAG   1    // the ROM loader talks over native USB

template <class PanelT, int kWidth, int kHeight, uint32_t kSpiHz, int kPinMiso>
class LGFX_FreenoveS3 : public lgfx::LGFX_Device
{
    PanelT          _panel;
    lgfx::Bus_SPI   _bus;
    lgfx::Light_PWM _light;
    lgfx::Touch_FT5x06 _touch;

public:
    LGFX_FreenoveS3()
    {
        {   // SPI2 on its own IOMUX pins (so 80 MHz is possible)
            auto cfg = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = kSpiHz;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = 12;
            cfg.pin_mosi    = 11;
            cfg.pin_miso    = kPinMiso;
            cfg.pin_dc      = 46;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {   // Panel: native portrait
            auto cfg = _panel.config();
            cfg.pin_cs           = 10;
            cfg.pin_rst          = -1;   // tied to EN
            cfg.pin_busy         = -1;
            cfg.panel_width      = kWidth;
            cfg.panel_height     = kHeight;
            cfg.memory_width     = kWidth;
            cfg.memory_height    = kHeight;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.readable         = kPinMiso >= 0;
            cfg.invert           = true;
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl      = 45;
            cfg.invert      = false;
            cfg.freq        = 44100;   // above hearing (see the CYD boards)
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {   // FT6336 (FT5x06 family) on the I2C bus the audio codec shares
            auto cfg = _touch.config();
            cfg.i2c_port        = BOARD_I2C_PORT;
            cfg.i2c_addr        = 0x38;
            cfg.pin_sda         = BOARD_PIN_I2C_SDA;
            cfg.pin_scl         = BOARD_PIN_I2C_SCL;
            cfg.pin_int         = -1;   // INT is on GPIO17; polled instead (its polarity unchecked)
            cfg.pin_rst         = 18;
            cfg.freq            = 400000;
            cfg.x_min           = 0;
            cfg.x_max           = kWidth - 1;
            cfg.y_min           = 0;
            cfg.y_max           = kHeight - 1;
            cfg.bus_shared      = false;
            cfg.offset_rotation = 0;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};
