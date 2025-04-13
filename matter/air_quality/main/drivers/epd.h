/*
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#pragma once

#include <M5Unified.h>
#include <lgfx/v1/panel/Panel_GDEW0154D67.hpp>

#include <esp_err.h>
#include <esp_log.h>

// Pin definitions for the e-paper display
#define EPD_MOSI 10  // SPI MOSI pin
#define EPD_MISO -1  // Not used, but required for SPI config
#define EPD_SCLK 11  // SPI SCLK pin
#define EPD_DC    9  // Data/Command control pin
#define EPD_CS    8  // Chip select pin
#define EPD_RST   18 // Reset pin
#define EPD_BUSY  19 // Busy pin
#define EPD_FREQ  20000000 // SPI frequency

// E-paper display class derived from LGFX_Device
class AirQ_EPD : public lgfx::LGFX_Device {
private:
    lgfx::Panel_GDEW0154D67 _panel_instance;
    lgfx::Bus_SPI _spi_bus_instance;

public:
    AirQ_EPD(void) {
        {
            auto cfg = _spi_bus_instance.config();
            cfg.pin_mosi = EPD_MOSI;
            cfg.pin_miso = EPD_MISO;
            cfg.pin_sclk = EPD_SCLK;
            cfg.pin_dc = EPD_DC;
            cfg.freq_write = EPD_FREQ;
            _spi_bus_instance.config(cfg);
            _panel_instance.setBus(&_spi_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.invert = false;
            cfg.pin_cs = EPD_CS;
            cfg.pin_rst = EPD_RST;
            cfg.pin_busy = EPD_BUSY;
            cfg.panel_width = 200;
            cfg.panel_height = 200;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
    
    bool begin(void) { return init_impl(true, false); }
};

// E-paper display driver initialization and control functions
esp_err_t epd_init(void);
esp_err_t epd_display_text(const char* text);
