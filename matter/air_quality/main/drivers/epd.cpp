/*
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "epd.h"

static const char *TAG = "epd";

static AirQ_EPD epd;
static M5Canvas canvas(&epd);

esp_err_t epd_init(void)
{
    ESP_LOGI(TAG, "Initializing e-paper display");
    
    if (!epd.begin()) {
        ESP_LOGE(TAG, "Failed to initialize e-paper display");
        return ESP_FAIL;
    }
    
    epd.setEpdMode(epd_mode_t::epd_fastest);
    
    canvas.createSprite(epd.width(), epd.height());
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_BLACK);
    canvas.setTextDatum(middle_center);
    
    epd.clear(TFT_WHITE);
    epd.waitDisplay();
    
    ESP_LOGI(TAG, "E-paper display initialized successfully");
    return ESP_OK;
}

esp_err_t epd_display_text(const char* text)
{
    ESP_LOGI(TAG, "Displaying text: %s", text);
    
    canvas.fillSprite(TFT_WHITE);
    
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.drawString(text, epd.width() / 2, epd.height() / 2);
    
    canvas.pushSprite(0, 0);
    epd.waitDisplay();
    
    return ESP_OK;
}
