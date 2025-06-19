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
#include <esp_matter_core.h>
#include <esp_matter_providers.h>
#include <lib/core/CHIPError.h>
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
#include <esp_timer.h>
#include <esp_heap_caps.h>

#include <inttypes.h>
#include <mutex>

static const char *TAG = "app_main";

// Custom Device Instance Information Provider Implementation
class CustomDeviceInstanceInfoProvider : public chip::DeviceLayer::DeviceInstanceInfoProvider {
public:
    CustomDeviceInstanceInfoProvider() = default;
    ~CustomDeviceInstanceInfoProvider() = default;

    // Basic device information methods
    CHIP_ERROR GetVendorName(char * buf, size_t bufSize) override {
        const char* vendorName = "M5Stack";
        size_t len = strlen(vendorName);
        if (len >= bufSize) {
            return CHIP_ERROR_BUFFER_TOO_SMALL;
        }
        strcpy(buf, vendorName);
        return CHIP_NO_ERROR;
    }
    
    CHIP_ERROR GetVendorId(uint16_t & vendorId) override {
        vendorId = 0xfff1; // Custom vendor ID
        return CHIP_NO_ERROR;
    }
    
    CHIP_ERROR GetProductName(char * buf, size_t bufSize) override {
        const char* productName = "Air Quality Monitor";
        size_t len = strlen(productName);
        if (len >= bufSize) {
            return CHIP_ERROR_BUFFER_TOO_SMALL;
        }
        strcpy(buf, productName);
        return CHIP_NO_ERROR;
    }
    
    CHIP_ERROR GetProductId(uint16_t & productId) override {
        productId = 0x8001;
        return CHIP_NO_ERROR;
    }
    
    CHIP_ERROR GetPartNumber(char * buf, size_t bufSize) override {
        const char* partNumber = "AQ-M5-001";
        size_t len = strlen(partNumber);
        if (len >= bufSize) {
            return CHIP_ERROR_BUFFER_TOO_SMALL;
        }
        strcpy(buf, partNumber);
        return CHIP_NO_ERROR;
    }
    
    CHIP_ERROR GetProductURL(char * buf, size_t bufSize) override {
        const char* productURL = "https://m5stack.com";
        size_t len = strlen(productURL);
        if (len >= bufSize) {
            return CHIP_ERROR_BUFFER_TOO_SMALL;
        }
        strcpy(buf, productURL);
        return CHIP_NO_ERROR;
    }
    
    CHIP_ERROR GetProductLabel(char * buf, size_t bufSize) override {
        const char* productLabel = "AirQuality-M5";
        size_t len = strlen(productLabel);
        if (len >= bufSize) {
            return CHIP_ERROR_BUFFER_TOO_SMALL;
        }
        strcpy(buf, productLabel);
        return CHIP_NO_ERROR;
    }
    
    CHIP_ERROR GetSerialNumber(char * buf, size_t bufSize) override {
        const char* serialNumber = "AQ001-001";
        size_t len = strlen(serialNumber);
        if (len >= bufSize) {
            return CHIP_ERROR_BUFFER_TOO_SMALL;
        }
        strcpy(buf, serialNumber);
        return CHIP_NO_ERROR;
    }
    
    CHIP_ERROR GetManufacturingDate(uint16_t & year, uint8_t & month, uint8_t & day) override {
        year = 2024;
        month = 6;
        day = 19;
        return CHIP_NO_ERROR;
    }
    
    CHIP_ERROR GetHardwareVersion(uint16_t & hardwareVersion) override {
        hardwareVersion = 1;
        return CHIP_NO_ERROR;
    }
    
    CHIP_ERROR GetHardwareVersionString(char * buf, size_t bufSize) override {
        const char* hwVersion = "1.0";
        size_t len = strlen(hwVersion);
        if (len >= bufSize) {
            return CHIP_ERROR_BUFFER_TOO_SMALL;
        }
        strcpy(buf, hwVersion);
        return CHIP_NO_ERROR;
    }
    
    CHIP_ERROR GetRotatingDeviceIdUniqueId(chip::MutableByteSpan & uniqueIdSpan) override {
        // Generate a simple unique ID based on serial number
        const char* serialStr = "AQ001-001";
        size_t serialLen = strlen(serialStr);
        if (uniqueIdSpan.size() < serialLen) {
            return CHIP_ERROR_BUFFER_TOO_SMALL;
        }
        memcpy(uniqueIdSpan.data(), serialStr, serialLen);
        uniqueIdSpan.reduce_size(serialLen);
        return CHIP_NO_ERROR;
    }
};

static CustomDeviceInstanceInfoProvider sCustomDeviceInstanceInfoProvider;


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
static TaskHandle_t s_epd_task_handle = nullptr;
static TaskHandle_t s_heap_monitor_task_handle = nullptr;

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

// PM sensor notifications using specific PM concentration measurement clusters for SEN55 sensor data
static void pm25_sensor_notification(uint16_t endpoint_id, float pm25_concentration, void *user_data)
{
    // schedule the attribute update so that we can report it from matter thread
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, pm25_concentration]() {
        esp_matter::attribute_t * attribute = esp_matter::attribute::get(endpoint_id,
                                                 Pm25ConcentrationMeasurement::Id,
                                                 Pm25ConcentrationMeasurement::Attributes::MeasuredValue::Id);

        esp_matter_attr_val_t val = esp_matter_invalid(NULL);
        esp_matter::attribute::get_val(attribute, &val);
        // Convert µg/m³ to float (Matter specification uses float for concentration)
        val.val.f = pm25_concentration;

        esp_matter::attribute::update(endpoint_id, Pm25ConcentrationMeasurement::Id, Pm25ConcentrationMeasurement::Attributes::MeasuredValue::Id, &val);
    });
}

static void pm10_sensor_notification(uint16_t endpoint_id, float pm10_concentration, void *user_data)
{
    // schedule the attribute update so that we can report it from matter thread
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, pm10_concentration]() {
        esp_matter::attribute_t * attribute = esp_matter::attribute::get(endpoint_id,
                                                 Pm10ConcentrationMeasurement::Id,
                                                 Pm10ConcentrationMeasurement::Attributes::MeasuredValue::Id);

        esp_matter_attr_val_t val = esp_matter_invalid(NULL);
        esp_matter::attribute::get_val(attribute, &val);
        val.val.f = pm10_concentration;

        esp_matter::attribute::update(endpoint_id, Pm10ConcentrationMeasurement::Id, Pm10ConcentrationMeasurement::Attributes::MeasuredValue::Id, &val);
    });
}

static void pm1_sensor_notification(uint16_t endpoint_id, float pm1_concentration, void *user_data)
{
    // schedule the attribute update so that we can report it from matter thread
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, pm1_concentration]() {
        esp_matter::attribute_t * attribute = esp_matter::attribute::get(endpoint_id,
                                                 Pm1ConcentrationMeasurement::Id,
                                                 Pm1ConcentrationMeasurement::Attributes::MeasuredValue::Id);

        esp_matter_attr_val_t val = esp_matter_invalid(NULL);
        esp_matter::attribute::get_val(attribute, &val);
        val.val.f = pm1_concentration;

        esp_matter::attribute::update(endpoint_id, Pm1ConcentrationMeasurement::Id, Pm1ConcentrationMeasurement::Attributes::MeasuredValue::Id, &val);
    });
}

static void co2_sensor_notification(uint16_t endpoint_id, float co2_concentration, void *user_data)
{
    // schedule the attribute update so that we can report it from matter thread
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, co2_concentration]() {
        esp_matter::attribute_t * attribute = esp_matter::attribute::get(endpoint_id,
                                                 CarbonDioxideConcentrationMeasurement::Id,
                                                 CarbonDioxideConcentrationMeasurement::Attributes::MeasuredValue::Id);

        esp_matter_attr_val_t val = esp_matter_invalid(NULL);
        esp_matter::attribute::get_val(attribute, &val);
        val.val.f = co2_concentration;

        esp_matter::attribute::update(endpoint_id, CarbonDioxideConcentrationMeasurement::Id, CarbonDioxideConcentrationMeasurement::Attributes::MeasuredValue::Id, &val);
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
    
#ifdef CONFIG_I2C_SCAN_ENABLE
    // Scan for I2C devices
    scan_i2c_bus();
#endif
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

    // Set temperature offset in sensor for accurate humidity compensation
    // Convert millicelsius offset to raw sensor value: (offset_celsius * 65535 / 175)
    uint16_t offset_raw = (uint16_t)((CONFIG_SCD4X_TEMP_OFFSET_MILLICELSIUS * 65535 / 1000) / 175);
    if (int16_t error = scd4x_set_temperature_offset_raw(offset_raw); error != NO_ERROR) {
        ESP_LOGE(TAG, "error executing scd4x_set_temperature_offset_raw(): %" PRIi16, error);
        return;
    }
    ESP_LOGI(TAG, "SCD4x temperature offset set to %dm°C (raw: %u)", CONFIG_SCD4X_TEMP_OFFSET_MILLICELSIUS, offset_raw);

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

#ifdef CONFIG_I2C_DEVICE_PROBE_ENABLE
    // Check if SEN55 is present at expected address (0x69)
    ESP_LOGI(TAG, "Probing SEN55 at address 0x69...");
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
#else
    ESP_LOGI(TAG, "Skipping I2C probe (disabled in config)");
#endif

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

typedef struct sen55_task_args_s {
    endpoint_t pm1_endpoint_id;
    endpoint_t pm25_endpoint_id;
    endpoint_t pm10_endpoint_id;
    endpoint_t voc_endpoint_id;
    endpoint_t nox_endpoint_id;
} sen55_task_args_t;

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
        ESP_LOGI(TAG, "SCD4x Measurement begin. (Free heap: %" PRIu32 " bytes)", esp_get_free_heap_size());

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
        
        // Print results in physical units (temperature and humidity already compensated by sensor)
        ESP_LOGI(TAG, "CO2 concentration [ppm]: %" PRIu16, co2_concentration);
        ESP_LOGI(TAG, "Temperature [m°C]: %" PRIi32, temperature);
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
        co2_sensor_notification(args->co2_endpoint_id, static_cast<float>(co2_concentration), nullptr);
    }

}

static void sen55_task(void* args_)
{
    auto args = static_cast<sen55_task_args_t*>(args_);

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
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(10000)); // SEN55 has 1Hz output rate
        ESP_LOGI(TAG, "SEN55 Measurement begin. (Free heap: %" PRIu32 " bytes)", esp_get_free_heap_size());

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

        // Update Matter attributes for air quality sensors
        pm1_sensor_notification(args->pm1_endpoint_id, mass_concentration_pm1p0 / 10.0f, nullptr);
        pm25_sensor_notification(args->pm25_endpoint_id, mass_concentration_pm2p5 / 10.0f, nullptr);
        pm10_sensor_notification(args->pm10_endpoint_id, mass_concentration_pm10p0 / 10.0f, nullptr);
        // Note: VOC and NOx don't have standard Matter clusters yet, would need custom implementation
    }
}

static void get_scd4x_data_safe(scd40_sensor_data_t* data)
{
    std::lock_guard<std::mutex> lock(s_sensor_data_mutex);
    *data = s_sensor_data;
}

static void get_sen55_data_safe(sen55_sensor_data_t* data)
{
    std::lock_guard<std::mutex> lock(s_sen55_data_mutex);
    *data = s_sen55_data;
}

static void epd_display_sensor_data()
{
    // Get sensor data safely
    scd40_sensor_data_t scd4x_data;
    sen55_sensor_data_t sen55_data;
    get_scd4x_data_safe(&scd4x_data);
    get_sen55_data_safe(&sen55_data);

    // Format display text for 200x200 EPD
    char display_text[512];
    snprintf(display_text, sizeof(display_text),
        "Air Quality Monitor\n"
        "\n"
        "SCD4x Sensor:\n"
        "CO2: %u ppm\n"
        "Temp: %.1f C\n"
        "Humidity: %.1f %%\n"
        "\n"
        "SEN55 Sensor:\n"
        "PM1.0: %.1f ug/m3\n"
        "PM2.5: %.1f ug/m3\n"
        "PM10: %.1f ug/m3\n"
        "VOC: %.1f\n"
        "NOx: %.1f\n"
        "\n"
        "Update: %llds",
        scd4x_data.co2_concentration,
        scd4x_data.temperature * 1.0e-3f,
        scd4x_data.relative_humidity * 1.0e-3f,
        sen55_data.mass_concentration_pm1p0 / 10.0f,
        sen55_data.mass_concentration_pm2p5 / 10.0f,
        sen55_data.mass_concentration_pm10p0 / 10.0f,
        sen55_data.voc_index / 10.0f,
        sen55_data.nox_index / 10.0f,
        esp_timer_get_time() / 1000000
    );

    ESP_LOGI(TAG, "Updating EPD display with sensor data");
    
    if (esp_err_t ret = epd_display_text(display_text); ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update EPD display: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "EPD display updated successfully");
    }
}

static void epd_task(void* args_)
{
    ESP_LOGI(TAG, "EPD display task started (update interval: %d seconds)", CONFIG_EPD_UPDATE_INTERVAL_SEC);
    
    // Update display immediately on startup
    ESP_LOGI(TAG, "EPD display initial update");
    epd_display_sensor_data();
    
    TickType_t wake_time = xTaskGetTickCount();
    const TickType_t update_interval = pdMS_TO_TICKS(CONFIG_EPD_UPDATE_INTERVAL_SEC * 1000);

    while(true) {
        vTaskDelayUntil(&wake_time, update_interval);
        
        ESP_LOGI(TAG, "EPD display update cycle begin");
        epd_display_sensor_data();
    }
}

static void heap_monitor_task(void* args_)
{
    ESP_LOGI(TAG, "Heap monitor task started (monitoring interval: 10 seconds)");
    
    TickType_t wake_time = xTaskGetTickCount();
    const TickType_t monitor_interval = pdMS_TO_TICKS(10000); // 10 seconds

    while(true) {
        vTaskDelayUntil(&wake_time, monitor_interval);
        
        size_t free_heap = esp_get_free_heap_size();
        uint32_t min_free_heap = esp_get_minimum_free_heap_size();
        size_t largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
        
        // Get internal RAM heap info
        size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        
        ESP_LOGI(TAG, "HEAP: Free=%zu Min=%" PRIu32 " LargestBlock=%zu Internal=%zu", free_heap, min_free_heap, largest_free_block, internal_free);
    }
}

extern "C" void app_main()
{
    /* Log initial heap state */
    ESP_LOGI(TAG, "=== STARTUP HEAP STATE ===");
    ESP_LOGI(TAG, "Initial free heap: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Initial largest free block: %zu bytes", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    ESP_LOGI(TAG, "Initial internal RAM free: %zu bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "==========================");

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

    /* Log heap state before Matter initialization */
    ESP_LOGI(TAG, "=== PRE-MATTER HEAP STATE ===");
    ESP_LOGI(TAG, "Free heap before Matter: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "==============================");

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    /* Log heap state after Matter node creation */
    ESP_LOGI(TAG, "=== POST-MATTER NODE HEAP STATE ===");
    ESP_LOGI(TAG, "Free heap after Matter node: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "===================================");

    /* Set up custom device instance information provider */
    ESP_LOGI(TAG, "Setting up custom device instance information provider");
#if CONFIG_CUSTOM_DEVICE_INSTANCE_INFO_PROVIDER
    esp_matter::set_custom_device_instance_info_provider(&sCustomDeviceInstanceInfoProvider);
    ESP_LOGI(TAG, "Custom device instance info provider registered successfully");
    ESP_LOGI(TAG, "Device: M5Stack Air Quality Monitor (Vendor: 0x1234, Product: 0x0001)");
#else
    ESP_LOGW(TAG, "CONFIG_CUSTOM_DEVICE_INSTANCE_INFO_PROVIDER not enabled");
#endif

    // add temperature sensor device
    temperature_sensor::config_t temp_sensor_config;
    endpoint_t * temp_sensor_ep = temperature_sensor::create(node, &temp_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(temp_sensor_ep != nullptr, ESP_LOGE(TAG, "Failed to create temperature_sensor endpoint"));

    // add the humidity sensor device
    humidity_sensor::config_t humidity_sensor_config;
    endpoint_t * humidity_sensor_ep = humidity_sensor::create(node, &humidity_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(humidity_sensor_ep != nullptr, ESP_LOGE(TAG, "Failed to create humidity_sensor endpoint"));

    // add air quality sensor devices for SEN55 PM measurements with specific PM concentration clusters
    air_quality_sensor::config_t pm1_sensor_config;
    endpoint_t * pm1_sensor_ep = air_quality_sensor::create(node, &pm1_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(pm1_sensor_ep != nullptr, ESP_LOGE(TAG, "Failed to create PM1.0 air_quality_sensor endpoint"));
    
    // Add PM1 concentration measurement cluster to the PM1 endpoint
    cluster_t * pm1_cluster = cluster::create(pm1_sensor_ep, Pm1ConcentrationMeasurement::Id, CLUSTER_FLAG_SERVER);
    ABORT_APP_ON_FAILURE(pm1_cluster != nullptr, ESP_LOGE(TAG, "Failed to create PM1 concentration measurement cluster"));
    // Add MeasuredValue attribute to PM1 cluster
    esp_matter_attr_val_t pm1_val = esp_matter_invalid(NULL);
    pm1_val.type = ESP_MATTER_VAL_TYPE_NULLABLE_FLOAT;
    pm1_val.val.f = 0.0f;
    attribute::create(pm1_cluster, Pm1ConcentrationMeasurement::Attributes::MeasuredValue::Id, ATTRIBUTE_FLAG_NULLABLE, pm1_val);

    air_quality_sensor::config_t pm25_sensor_config;
    endpoint_t * pm25_sensor_ep = air_quality_sensor::create(node, &pm25_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(pm25_sensor_ep != nullptr, ESP_LOGE(TAG, "Failed to create PM2.5 air_quality_sensor endpoint"));
    
    // Add PM2.5 concentration measurement cluster to the PM2.5 endpoint
    cluster_t * pm25_cluster = cluster::create(pm25_sensor_ep, Pm25ConcentrationMeasurement::Id, CLUSTER_FLAG_SERVER);
    ABORT_APP_ON_FAILURE(pm25_cluster != nullptr, ESP_LOGE(TAG, "Failed to create PM2.5 concentration measurement cluster"));
    // Add MeasuredValue attribute to PM2.5 cluster
    esp_matter_attr_val_t pm25_val = esp_matter_invalid(NULL);
    pm25_val.type = ESP_MATTER_VAL_TYPE_NULLABLE_FLOAT;
    pm25_val.val.f = 0.0f;
    attribute::create(pm25_cluster, Pm25ConcentrationMeasurement::Attributes::MeasuredValue::Id, ATTRIBUTE_FLAG_NULLABLE, pm25_val);

    air_quality_sensor::config_t pm10_sensor_config;
    endpoint_t * pm10_sensor_ep = air_quality_sensor::create(node, &pm10_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(pm10_sensor_ep != nullptr, ESP_LOGE(TAG, "Failed to create PM10 air_quality_sensor endpoint"));
    
    // Add PM10 concentration measurement cluster to the PM10 endpoint
    cluster_t * pm10_cluster = cluster::create(pm10_sensor_ep, Pm10ConcentrationMeasurement::Id, CLUSTER_FLAG_SERVER);
    ABORT_APP_ON_FAILURE(pm10_cluster != nullptr, ESP_LOGE(TAG, "Failed to create PM10 concentration measurement cluster"));
    // Add MeasuredValue attribute to PM10 cluster
    esp_matter_attr_val_t pm10_val = esp_matter_invalid(NULL);
    pm10_val.type = ESP_MATTER_VAL_TYPE_NULLABLE_FLOAT;
    pm10_val.val.f = 0.0f;
    attribute::create(pm10_cluster, Pm10ConcentrationMeasurement::Attributes::MeasuredValue::Id, ATTRIBUTE_FLAG_NULLABLE, pm10_val);

    // add CO2 concentration sensor endpoint for SCD4x CO2 measurements
    air_quality_sensor::config_t co2_sensor_config;
    endpoint_t * co2_sensor_ep = air_quality_sensor::create(node, &co2_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(co2_sensor_ep != nullptr, ESP_LOGE(TAG, "Failed to create CO2 air_quality_sensor endpoint"));
    
    // Add CO2 concentration measurement cluster to the CO2 endpoint
    cluster_t * co2_cluster = cluster::create(co2_sensor_ep, CarbonDioxideConcentrationMeasurement::Id, CLUSTER_FLAG_SERVER);
    ABORT_APP_ON_FAILURE(co2_cluster != nullptr, ESP_LOGE(TAG, "Failed to create CO2 concentration measurement cluster"));
    // Add MeasuredValue attribute to CO2 cluster
    esp_matter_attr_val_t co2_val = esp_matter_invalid(NULL);
    co2_val.type = ESP_MATTER_VAL_TYPE_NULLABLE_FLOAT;
    co2_val.val.f = 0.0f;
    attribute::create(co2_cluster, CarbonDioxideConcentrationMeasurement::Attributes::MeasuredValue::Id, ATTRIBUTE_FLAG_NULLABLE, co2_val);

    /* Log heap state after all endpoints creation */
    ESP_LOGI(TAG, "=== POST-ENDPOINTS HEAP STATE ===");
    ESP_LOGI(TAG, "Free heap after endpoints: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "==================================");

    
    /* Initialize shared I2C bus for sensors */
    initialize_i2c_bus();

    /* Initialize the SCD4x sensor */
    initialize_scd4x();
    scd4x_task_args_t* scd4x_task_args = new scd4x_task_args_t;
    scd4x_task_args->temperature_endpoint_id = endpoint::get_id(temp_sensor_ep);
    scd4x_task_args->humidity_endpoint_id = endpoint::get_id(humidity_sensor_ep);
    scd4x_task_args->co2_endpoint_id = endpoint::get_id(co2_sensor_ep);
    if( BaseType_t result = xTaskCreatePinnedToCore(scd4x_task, "scd4x_task", 3072, scd4x_task_args, 5, &s_sensor_task_handle, APP_CPU_NUM); result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SCD4x task");
        abort();
    }

    /* Initialize the SEN55 sensor */
    initialize_sen55();
    sen55_task_args_t* sen55_task_args = new sen55_task_args_t;
    sen55_task_args->pm1_endpoint_id = endpoint::get_id(pm1_sensor_ep);
    sen55_task_args->pm25_endpoint_id = endpoint::get_id(pm25_sensor_ep);
    sen55_task_args->pm10_endpoint_id = endpoint::get_id(pm10_sensor_ep);
    sen55_task_args->voc_endpoint_id = 0; // VOC not implemented yet
    sen55_task_args->nox_endpoint_id = 0; // NOx not implemented yet
    if( BaseType_t result = xTaskCreatePinnedToCore(sen55_task, "sen55_task", 3072, sen55_task_args, 5, &s_sen55_task_handle, APP_CPU_NUM); result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SEN55 task");
        abort();
    }

    /* Start EPD display task */
    if( BaseType_t result = xTaskCreatePinnedToCore(epd_task, "epd_task", 3072, nullptr, 3, &s_epd_task_handle, APP_CPU_NUM); result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create EPD display task");
        abort();
    }

    /* Start heap monitor task for debugging memory issues */
    if( BaseType_t result = xTaskCreatePinnedToCore(heap_monitor_task, "heap_monitor", 3072, nullptr, 1, &s_heap_monitor_task_handle, APP_CPU_NUM); result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create heap monitor task");
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

    /* Log heap state before Matter start */
    ESP_LOGI(TAG, "=== PRE-MATTER START HEAP STATE ===");
    ESP_LOGI(TAG, "Free heap before Matter start: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "===================================");

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    /* Log final heap state after Matter start */
    ESP_LOGI(TAG, "=== FINAL HEAP STATE ===");
    ESP_LOGI(TAG, "Free heap after Matter start: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Minimum free heap ever: %" PRIu32 " bytes", esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "========================");
}
