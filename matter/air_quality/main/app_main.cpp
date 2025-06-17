/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <M5Unified.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <bsp/esp-bsp.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_ota.h>
#include <nvs_flash.h>

#include <app_openthread_config.h>
#include <app_reset.h>
#include <common_macros.h>

#include <drivers/epd.h>

#include <scd4x_i2c.h>
#include <sen5x_i2c.h>
#include <sensirion_common.h>
#include <sensirion_i2c_hal.h>
#include <sensirion_i2c_esp32_config.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2c.h>
#include <driver/gpio.h>

#include <inttypes.h>
#include <mutex>

static const char *TAG = "app_main";

// I2C bus configuration - shared between sensors
static bool s_i2c_initialized = false;

// SEN55 power control
#define SEN55_POWER_GPIO GPIO_NUM_10

typedef struct scd40_sensor_data_s {
    uint16_t co2_concentration;
    int32_t temperature;
    int32_t relative_humidity;
} scd40_sensor_data_t;

typedef struct sen55_sensor_data_s {
    uint16_t mass_concentration_pm1p0;
    uint16_t mass_concentration_pm2p5;
    uint16_t mass_concentration_pm4p0;
    uint16_t mass_concentration_pm10p0;
    int16_t ambient_humidity;
    int16_t ambient_temperature;
    int16_t voc_index;
    int16_t nox_index;
} sen55_sensor_data_t;

static scd40_sensor_data_t s_sensor_data = {0};
static sen55_sensor_data_t s_sen55_data = {0};
static std::mutex s_sensor_data_mutex;
static std::mutex s_sen55_data_mutex;
static TaskHandle_t s_sensor_task_handle = nullptr;
static TaskHandle_t s_sen55_task_handle = nullptr;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

// Application cluster specification, 7.18.2.11. Temperature
// represents a temperature on the Celsius scale with a resolution of 0.01°C.
// temp = (temperature in °C) x 100
static void temp_sensor_notification(uint16_t endpoint_id, float temp, void *user_data)
{
    // schedule the attribute update so that we can report it from matter thread
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, temp]() {
        esp_matter::attribute_t * attribute = esp_matter::attribute::get(endpoint_id,
                                                 TemperatureMeasurement::Id,
                                                 TemperatureMeasurement::Attributes::MeasuredValue::Id);

        esp_matter_attr_val_t val = esp_matter_invalid(NULL);
        esp_matter::attribute::get_val(attribute, &val);
        val.val.i16 = static_cast<int16_t>(temp * 100);

        esp_matter::attribute::update(endpoint_id, TemperatureMeasurement::Id, TemperatureMeasurement::Attributes::MeasuredValue::Id, &val);
    });
}

// Application cluster specification, 2.6.4.1. MeasuredValue Attribute
// represents the humidity in percent.
// humidity = (humidity in %) x 100
static void humidity_sensor_notification(uint16_t endpoint_id, float humidity, void *user_data)
{
    // schedule the attribute update so that we can report it from matter thread
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, humidity]() {
        esp_matter::attribute_t * attribute = esp_matter::attribute::get(endpoint_id,
                                                 RelativeHumidityMeasurement::Id,
                                                 RelativeHumidityMeasurement::Attributes::MeasuredValue::Id);

        esp_matter_attr_val_t val = esp_matter_invalid(NULL);
        esp_matter::attribute::get_val(attribute, &val);
        val.val.u16 = static_cast<uint16_t>(humidity * 100);

        esp_matter::attribute::update(endpoint_id, RelativeHumidityMeasurement::Id, RelativeHumidityMeasurement::Attributes::MeasuredValue::Id, &val);
    });
}

static void occupancy_sensor_notification(uint16_t endpoint_id, bool occupancy, void *user_data)
{
    // schedule the attribute update so that we can report it from matter thread
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, occupancy]() {
        esp_matter::attribute_t * attribute = esp_matter::attribute::get(endpoint_id,
                                                 OccupancySensing::Id,
                                                 OccupancySensing::Attributes::Occupancy::Id);

        esp_matter_attr_val_t val = esp_matter_invalid(NULL);
        esp_matter::attribute::get_val(attribute, &val);
        val.val.b = occupancy;

        esp_matter::attribute::update(endpoint_id, OccupancySensing::Id, OccupancySensing::Attributes::Occupancy::Id, &val);
    });
}

static esp_err_t factory_reset_button_register()
{
    button_handle_t push_button;
    esp_err_t err = bsp_iot_button_create(&push_button, NULL, BSP_BUTTON_NUM);
    VerifyOrReturnError(err == ESP_OK, err);
    return app_reset_button_register(push_button);
}

static void open_commissioning_window_if_necessary()
{
    VerifyOrReturn(chip::Server::GetInstance().GetFabricTable().FabricCount() == 0);

    chip::CommissioningWindowManager & commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    VerifyOrReturn(commissionMgr.IsCommissioningWindowOpen() == false);

    // After removing last fabric, this example does not remove the Wi-Fi credentials
    // and still has IP connectivity so, only advertising on DNS-SD.
    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds16(300),
                                    chip::CommissioningWindowAdvertisement::kDnssdOnly);
    if (err != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
    }
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed successfully");
        open_commissioning_window_if_necessary();
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;

    default:
        break;
    }
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or light).
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    epd_display_text("Identifying");
    return ESP_OK;
}

// This callback is called for every attribute update. The callback implementation shall
// handle the desired attributes and return an appropriate error code. If the attribute
// is not of your interest, please do not return an error code and strictly return ESP_OK.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    // Since this is just a sensor and we don't expect any writes on our temperature sensor,
    // so, return success.
    return ESP_OK;
}

static void scan_i2c_bus()
{
    ESP_LOGI(TAG, "Scanning I2C bus...");
    uint8_t devices_found = 0;
    
    i2c_cmd_handle_t cmd;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        // Try to communicate with device at this address
        cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 50 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "I2C device found at address 0x%02x", addr);
            devices_found++;
        }
    }
    
    ESP_LOGI(TAG, "I2C scan complete. Found %d devices", devices_found);
}

static void initialize_i2c_bus()
{
    if (s_i2c_initialized) {
        ESP_LOGI(TAG, "I2C bus already initialized");
        return;
    }

    esp32_i2c_config_t config {
        100000, // freq
        0x00, // addr - not used for initialization
        I2C_NUM_0, // port
        GPIO_NUM_11, // sda
        GPIO_NUM_12, // scl
        true, // enable_pullup
    };

    ESP_LOGI(TAG, "Initializing I2C bus on port %d (SDA: GPIO%d, SCL: GPIO%d)", 
             config.port, config.sda, config.scl);
    
    sensirion_i2c_config_esp32(&config);
    sensirion_i2c_hal_init();
    
    s_i2c_initialized = true;
    ESP_LOGI(TAG, "I2C bus initialization complete");
    
    // Scan for I2C devices
    scan_i2c_bus();
}

static void initialize_scd4x()
{
    // I2C bus should already be initialized
    if (!s_i2c_initialized) {
        ESP_LOGE(TAG, "I2C bus not initialized before SCD4x initialization");
        return;
    }

    scd4x_init(SCD41_I2C_ADDR_62);

    if (int16_t error = scd4x_wake_up(); error != NO_ERROR) {
        ESP_LOGE(TAG, "error executing wake_up(): %" PRIi16, error);
        return;
    }

    if (int16_t error = scd4x_stop_periodic_measurement(); error != NO_ERROR) {
        ESP_LOGE(TAG, "error executing stop_periodic_measurement(): %" PRIi16, error);
        return;
    }

    if (int16_t error = scd4x_reinit(); error != NO_ERROR) {
        ESP_LOGE(TAG, "error executing reinit(): %" PRIi16, error);
        return;
    }

    // Read out information about the sensor
    uint16_t serial_number[3] = {0};
    if (int16_t error = scd4x_get_serial_number(serial_number, 3); error != NO_ERROR) {
        ESP_LOGE(TAG, "error executing get_serial_number(): %" PRIi16, error);
        return;
    }
    uint64_t serial_as_int = 0;
    sensirion_common_to_integer((uint8_t*)serial_number, (uint8_t*)&serial_as_int,
                                LONG_INTEGER, 6);
    ESP_LOGI(TAG, "serial number: 0x%" PRIx64, serial_as_int);

    return;
}

static void configure_sen55_power()
{
    ESP_LOGI(TAG, "Configuring SEN55 power control on GPIO%d", SEN55_POWER_GPIO);
    
    // Configure GPIO10 as output for SEN55 power control
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SEN55_POWER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (esp_err_t ret = gpio_config(&io_conf); ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure SEN55 power GPIO: %s", esp_err_to_name(ret));
        return;
    }
    
    // Set GPIO10 to LOW to enable SEN55 power
    if (esp_err_t ret = gpio_set_level(SEN55_POWER_GPIO, 0); ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set SEN55 power GPIO to LOW: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "SEN55 power enabled (GPIO%d set to LOW)", SEN55_POWER_GPIO);
    
    // Wait for SEN55 to power up
    ESP_LOGI(TAG, "Waiting 1 second for SEN55 to power up...");
    vTaskDelay(pdMS_TO_TICKS(1000));
}

static void initialize_sen55()
{
    ESP_LOGI(TAG, "Starting SEN55 initialization...");
    
    // I2C bus should already be initialized
    if (!s_i2c_initialized) {
        ESP_LOGE(TAG, "I2C bus not initialized before SEN55 initialization");
        return;
    }
    ESP_LOGI(TAG, "I2C bus is initialized, proceeding with SEN55 setup");

    // Configure and enable SEN55 power
    configure_sen55_power();

    // Check if SEN55 is present at expected address (0x69)
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (0x69 << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t probe_result = i2c_master_cmd_begin(I2C_NUM_0, cmd, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    
    if (probe_result != ESP_OK) {
        ESP_LOGE(TAG, "SEN55 not found at address 0x69, probe result: %s", esp_err_to_name(probe_result));
        ESP_LOGE(TAG, "Please check SEN55 wiring and power supply");
        return;
    }
    ESP_LOGI(TAG, "SEN55 detected at address 0x69");

    // SEN5X does not have an explicit init function, start with device reset
    ESP_LOGI(TAG, "Resetting SEN55 device...");
    if (int16_t error = sen5x_device_reset(); error != NO_ERROR) {
        ESP_LOGE(TAG, "error executing sen5x_device_reset(): %" PRIi16, error);
        ESP_LOGE(TAG, "SEN55 device reset failed - aborting initialization");
        return;
    }
    ESP_LOGI(TAG, "SEN55 device reset successful");

    // Wait for sensor to reset
    ESP_LOGI(TAG, "Waiting 1 second for SEN55 to complete reset...");
    sensirion_i2c_hal_sleep_usec(1000000);

    // Read serial number
    ESP_LOGI(TAG, "Reading SEN55 serial number...");
    unsigned char serial_number[32] = {0};
    uint8_t serial_number_size = 32;
    if (int16_t error = sen5x_get_serial_number(serial_number, serial_number_size); error != NO_ERROR) {
        ESP_LOGE(TAG, "error executing sen5x_get_serial_number(): %" PRIi16, error);
        ESP_LOGE(TAG, "Failed to read SEN55 serial number - continuing anyway");
    } else {
        ESP_LOGI(TAG, "SEN55 serial number: %s", serial_number);
    }

    // Read product name
    ESP_LOGI(TAG, "Reading SEN55 product name...");
    unsigned char product_name[32] = {0};
    uint8_t product_name_size = 32;
    if (int16_t error = sen5x_get_product_name(product_name, product_name_size); error != NO_ERROR) {
        ESP_LOGE(TAG, "error executing sen5x_get_product_name(): %" PRIi16, error);
        ESP_LOGE(TAG, "Failed to read SEN55 product name - continuing anyway");
    } else {
        ESP_LOGI(TAG, "SEN55 product name: %s", product_name);
    }

    ESP_LOGI(TAG, "SEN55 initialization completed successfully");
    return;
}

static void dump_scd4x()
{
    int16_t error = scd4x_start_periodic_measurement();
    if (error != NO_ERROR) {
        ESP_LOGE(TAG, "error executing start_periodic_measurement(): %" PRIi16, error);
        return;
    }
    //
    // If low-power mode is required, switch to the low power
    // measurement function instead of the standard measurement
    // function above. Check out the header file for the definition.
    //
    bool data_ready = false;
    uint16_t co2_concentration = 0;
    int32_t temperature = 0;
    int32_t relative_humidity = 0;
    uint16_t repetition = 0;
    for (repetition = 0; repetition < 1; repetition++) {
        //
        // Slow down the sampling to 0.2Hz.
        //
        sensirion_i2c_hal_sleep_usec(5000000);
        //
        // If ambient pressure compensation during measurement
        // is required, you should call the respective functions here.
        // Check out the header file for the function definition.
        error = scd4x_get_data_ready_status(&data_ready);
        if (error != NO_ERROR) {
            ESP_LOGE(TAG, "error executing get_data_ready_status(): %" PRIi16, error);
            continue;
        }
        while (!data_ready) {
            sensirion_i2c_hal_sleep_usec(100000);
            error = scd4x_get_data_ready_status(&data_ready);
            if (error != NO_ERROR) {
                ESP_LOGE(TAG, "error executing get_data_ready_status(): %" PRIi16, error);
                continue;
            }
        }
        error = scd4x_read_measurement(&co2_concentration, &temperature,
                                       &relative_humidity);
        if (error != NO_ERROR) {
            ESP_LOGE(TAG, "error executing read_measurement(): %" PRIi16, error);
            continue;
        }
        
        // Print results in physical units.
        ESP_LOGI(TAG, "CO2 concentration [ppm]: %" PRIu16, co2_concentration);
        ESP_LOGI(TAG, "Temperature [m°C] : %" PRIi32, temperature);
        ESP_LOGI(TAG, "Humidity [mRH]: %" PRIi32, relative_humidity);
    }
}

typedef struct scd4x_task_args_s {
    endpoint_t temperature_endpoint_id;
    endpoint_t humidity_endpoint_id;
    endpoint_t co2_endpoint_id;
} scd4x_task_args_t;

static void scd4x_task(void* args_)
{
    auto args = static_cast<scd4x_task_args_t*>(args_);

    ESP_LOGI(TAG, "SCD4x Measurement task started");
        
    TickType_t wake_time = xTaskGetTickCount();

    if (int16_t error = scd4x_start_periodic_measurement(); error != NO_ERROR) {
        ESP_LOGE(TAG, "error executing start_periodic_measurement(): %" PRIi16, error);
        abort();
    }

    while(true) {
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(30000));
        ESP_LOGI(TAG, "SCD4x Measurement begin.");

        bool data_ready = false;
        while(true) {
            int16_t error = scd4x_get_data_ready_status(&data_ready);
            if (error != NO_ERROR) {
                ESP_LOGE(TAG, "error executing get_data_ready_status(): %" PRIi16, error);
                continue;
            }
            if( data_ready ) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        uint16_t co2_concentration = 0;
        int32_t temperature = 0;
        int32_t relative_humidity = 0;

        if (int16_t error = scd4x_read_measurement(&co2_concentration, &temperature, &relative_humidity); error != NO_ERROR) {
            ESP_LOGE(TAG, "error executing read_measurement(): %" PRIi16, error);
            continue;
        }
        
        // Print results in physical units.
        ESP_LOGI(TAG, "CO2 concentration [ppm]: %" PRIu16, co2_concentration);
        ESP_LOGI(TAG, "Temperature [m°C] : %" PRIi32, temperature);
        ESP_LOGI(TAG, "Humidity [mRH]: %" PRIi32, relative_humidity);

        // Lock and update the sensor data
        {
            std::lock_guard<std::mutex> lock(s_sensor_data_mutex);
            s_sensor_data.co2_concentration = co2_concentration;
            s_sensor_data.temperature = temperature;
            s_sensor_data.relative_humidity = relative_humidity;
        }

        // Update Matter attributes
        temp_sensor_notification(args->temperature_endpoint_id, temperature * 1.0e-3f, nullptr);
        humidity_sensor_notification(args->humidity_endpoint_id, relative_humidity * 1.0e-3f, nullptr);
        // TODO: Update CO2 sensor
    }

}

static void sen55_task(void* args_)
{
    ESP_LOGI(TAG, "SEN55 Measurement task started");

    TickType_t wake_time = xTaskGetTickCount();

    // Try to start measurement, with retries
    int retry_count = 0;
    const int max_retries = 3;
    
    while (retry_count < max_retries) {
        if (int16_t error = sen5x_start_measurement(); error != NO_ERROR) {
            ESP_LOGE(TAG, "error executing sen5x_start_measurement() (attempt %d/%d): %" PRIi16, 
                     retry_count + 1, max_retries, error);
            retry_count++;
            if (retry_count >= max_retries) {
                ESP_LOGE(TAG, "Failed to start SEN55 measurement after %d attempts, task will exit", max_retries);
                vTaskDelete(NULL);
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second before retry
        } else {
            ESP_LOGI(TAG, "SEN55 measurement started successfully");
            break;
        }
    }

    while(true) {
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(1000)); // SEN55 has 1Hz output rate
        ESP_LOGI(TAG, "SEN55 Measurement begin.");

        bool data_ready = false;
        while(true) {
            if (int16_t error = sen5x_read_data_ready(&data_ready); error != NO_ERROR) {
                ESP_LOGE(TAG, "error executing sen5x_read_data_ready(): %" PRIi16, error);
                continue;
            }
            if( data_ready ) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        uint16_t mass_concentration_pm1p0;
        uint16_t mass_concentration_pm2p5;
        uint16_t mass_concentration_pm4p0;
        uint16_t mass_concentration_pm10p0;
        int16_t ambient_humidity;
        int16_t ambient_temperature;
        int16_t voc_index;
        int16_t nox_index;

        if (int16_t error = sen5x_read_measured_values(
            &mass_concentration_pm1p0, &mass_concentration_pm2p5,
            &mass_concentration_pm4p0, &mass_concentration_pm10p0,
            &ambient_humidity, &ambient_temperature, &voc_index,
            &nox_index); error != NO_ERROR) {
            ESP_LOGE(TAG, "error executing sen5x_read_measured_values(): %" PRIi16, error);
            continue;
        }

        // Print results in physical units (values are scaled by factor 10 or 100)
        ESP_LOGI(TAG, "PM1.0 concentration [µg/m³]: %.1f", mass_concentration_pm1p0 / 10.0f);
        ESP_LOGI(TAG, "PM2.5 concentration [µg/m³]: %.1f", mass_concentration_pm2p5 / 10.0f);
        ESP_LOGI(TAG, "PM4.0 concentration [µg/m³]: %.1f", mass_concentration_pm4p0 / 10.0f);
        ESP_LOGI(TAG, "PM10.0 concentration [µg/m³]: %.1f", mass_concentration_pm10p0 / 10.0f);
        ESP_LOGI(TAG, "Ambient humidity [%%RH]: %.1f", ambient_humidity / 100.0f);
        ESP_LOGI(TAG, "Ambient temperature [°C]: %.1f", ambient_temperature / 200.0f);
        ESP_LOGI(TAG, "VOC index: %.1f", voc_index / 10.0f);
        ESP_LOGI(TAG, "NOx index: %.1f", nox_index / 10.0f);

        // Lock and update the sensor data
        {
            std::lock_guard<std::mutex> lock(s_sen55_data_mutex);
            s_sen55_data.mass_concentration_pm1p0 = mass_concentration_pm1p0;
            s_sen55_data.mass_concentration_pm2p5 = mass_concentration_pm2p5;
            s_sen55_data.mass_concentration_pm4p0 = mass_concentration_pm4p0;
            s_sen55_data.mass_concentration_pm10p0 = mass_concentration_pm10p0;
            s_sen55_data.ambient_humidity = ambient_humidity;
            s_sen55_data.ambient_temperature = ambient_temperature;
            s_sen55_data.voc_index = voc_index;
            s_sen55_data.nox_index = nox_index;
        }

        // Note: Matter endpoint updates would go here if implemented
    }
}

extern "C" void app_main()
{
    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    /* Initialize push button on the dev-kit to reset the device */
    esp_err_t err = factory_reset_button_register();
    ABORT_APP_ON_FAILURE(ESP_OK == err, ESP_LOGE(TAG, "Failed to initialize reset button, err:%d", err));

    /* Initialize e-paper display */
    err = epd_init();
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to initialize e-paper display, err:%d", err));

    /* Display Hello World on the e-paper display */
    err = epd_display_text("Hello World");
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to display text on e-paper display, err:%d", err));

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    // add temperature sensor device
    temperature_sensor::config_t temp_sensor_config;
    endpoint_t * temp_sensor_ep = temperature_sensor::create(node, &temp_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(temp_sensor_ep != nullptr, ESP_LOGE(TAG, "Failed to create temperature_sensor endpoint"));

    // add the humidity sensor device
    humidity_sensor::config_t humidity_sensor_config;
    endpoint_t * humidity_sensor_ep = humidity_sensor::create(node, &humidity_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(humidity_sensor_ep != nullptr, ESP_LOGE(TAG, "Failed to create humidity_sensor endpoint"));

    
    /* Initialize shared I2C bus for sensors */
    initialize_i2c_bus();

    /* Initialize the SCD4x sensor */
    initialize_scd4x();
    scd4x_task_args_t* scd4x_task_args = new scd4x_task_args_t;
    scd4x_task_args->temperature_endpoint_id = endpoint::get_id(temp_sensor_ep);
    scd4x_task_args->humidity_endpoint_id = endpoint::get_id(humidity_sensor_ep);
    //scd4x_task_args->co2_endpoint_id = endpoint::get_id(co2_sensor_ep);
    if( BaseType_t result = xTaskCreatePinnedToCore(scd4x_task, "scd4x_task", 4096, scd4x_task_args, 5, &s_sensor_task_handle, APP_CPU_NUM); result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SCD4x task");
        abort();
    }

    /* Initialize the SEN55 sensor */
    initialize_sen55();
    if( BaseType_t result = xTaskCreatePinnedToCore(sen55_task, "sen55_task", 4096, nullptr, 5, &s_sen55_task_handle, APP_CPU_NUM); result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SEN55 task");
        abort();
    }

    // // add the occupancy sensor
    // occupancy_sensor::config_t occupancy_sensor_config;
    // occupancy_sensor_config.occupancy_sensing.occupancy_sensor_type =
    //     chip::to_underlying(OccupancySensing::OccupancySensorTypeEnum::kPir);
    // occupancy_sensor_config.occupancy_sensing.occupancy_sensor_type_bitmap =
    //     chip::to_underlying(OccupancySensing::OccupancySensorTypeBitmap::kPir);

    // endpoint_t * occupancy_sensor_ep = occupancy_sensor::create(node, &occupancy_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    // ABORT_APP_ON_FAILURE(occupancy_sensor_ep != nullptr, ESP_LOGE(TAG, "Failed to create occupancy_sensor endpoint"));

    // // initialize occupancy sensor driver (pir)
    // static pir_sensor_config_t pir_config = {
    //     .cb = occupancy_sensor_notification,
    //     .endpoint_id = endpoint::get_id(occupancy_sensor_ep),
    // };
    // err = pir_sensor_init(&pir_config);
    // ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to initialize occupancy sensor driver"));

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));
}
