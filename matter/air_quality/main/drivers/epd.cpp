/*
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "epd.h"
#include <cstring>

static const char *TAG = "epd";

static AirQ_EPD epd;

esp_err_t epd_init(void)
{
    ESP_LOGI(TAG, "Initializing e-paper display");
    
    if (!epd.begin()) {
        ESP_LOGE(TAG, "Failed to initialize e-paper display");
        return ESP_FAIL;
    }
    
    epd.setEpdMode(epd_mode_t::epd_fastest);
    epd.setTextSize(1);
    epd.setTextColor(TFT_BLACK);
    epd.setTextDatum(top_left);
    epd.setFont(&fonts::Font0);
    
    epd.clear(TFT_WHITE);
    epd.waitDisplay();
    
    ESP_LOGI(TAG, "E-paper display initialized successfully");
    return ESP_OK;
}

esp_err_t epd_display_text(const char* text)
{
    ESP_LOGI(TAG, "Displaying sensor data");
    
    // Clear display and draw directly to EPD
    epd.clear(TFT_WHITE);
    
    // Split text into lines and display with proper spacing
    const char* line_start = text;
    const char* line_end;
    int y = 3;  // Start 3 pixels from top
    const int line_height = 10;  // Line spacing for Font0 + 2px padding
    
    while (*line_start && y < epd.height() - line_height) {
        // Find end of current line
        line_end = strchr(line_start, '\n');
        if (line_end == nullptr) {
            line_end = line_start + strlen(line_start);
        }
        
        // Create temporary string for current line
        size_t line_len = line_end - line_start;
        char line_buffer[128];
        if (line_len < sizeof(line_buffer)) {
            strncpy(line_buffer, line_start, line_len);
            line_buffer[line_len] = '\0';
            
            // Draw the line directly to EPD
            epd.drawString(line_buffer, 5, y);
            y += line_height;
        }
        
        // Move to next line
        if (*line_end == '\n') {
            line_start = line_end + 1;
        } else {
            break;  // End of string
        }
    }
    
    epd.waitDisplay();
    
    return ESP_OK;
}
