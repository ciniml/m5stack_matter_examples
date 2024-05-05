/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#if CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif
#include <esp_sleep.h>
#include <driver/rtc_io.h>

#if CONFIG_ESP_SLEEP_DEBUG
#include <esp_timer.h>
#include <esp_private/esp_pmu.h>
#include <esp_private/esp_sleep_internal.h>
#endif
#include "ulp_lp_core.h"

#include <esp_matter.h>
#include <esp_matter_ota.h>

#include <app_priv.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <app-common/zap-generated/attributes/Accessors.h>

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

// LP core firmware
extern const uint8_t lp_core_main_bin_start[] asm("_binary_lp_core_main_bin_start");
extern const uint8_t lp_core_main_bin_end[]   asm("_binary_lp_core_main_bin_end");

constexpr auto k_timeout_seconds = 300;

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        {
            ESP_LOGI(TAG, "Fabric removed successfully");
            if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0)
            {
                chip::CommissioningWindowManager & commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
                constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
                if (!commissionMgr.IsCommissioningWindowOpen())
                {
                    /* After removing last fabric, this example does not remove the Wi-Fi credentials
                     * and still has IP connectivity so, only advertising on DNS-SD.
                     */
                    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds,
                                                    chip::CommissioningWindowAdvertisement::kDnssdOnly);
                    if (err != CHIP_NO_ERROR)
                    {
                        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
                    }
                }
            }
        break;
        }

    case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
        ESP_LOGI(TAG, "Fabric will be removed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
        ESP_LOGI(TAG, "Fabric is updated");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
        ESP_LOGI(TAG, "Fabric is committed");
        break;
    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    esp_err_t err = ESP_OK;

    if (type == PRE_UPDATE) {
        /* Driver update */
    }

    return err;
}

static endpoint_t *sensor_endpoint = nullptr;
static QueueHandle_t button_gpio_event_queue = nullptr;
static constexpr gpio_num_t button_gpio_num = (gpio_num_t)2;

static void IRAM_ATTR button_gpio_isr_handler(void *arg)
{
    auto gpio_num = static_cast<gpio_num_t>(reinterpret_cast<uintptr_t>(arg));
    if( button_gpio_event_queue != nullptr ) {
        xQueueSendFromISR(button_gpio_event_queue, &gpio_num, nullptr);
    }
}

static bool button_is_pushed()
{
    return rtc_gpio_get_level(button_gpio_num) == 0;
    //return gpio_get_level(button_gpio_num) == 0;
}

static void IRAM_ATTR button_setup_interrupt()
{
    //ESP_ERROR_CHECK(gpio_wakeup_disable(button_gpio_num));
    ESP_ERROR_CHECK(gpio_set_intr_type(button_gpio_num, GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_intr_enable(button_gpio_num));
}

static void button_gpio_event_task(void *arg)
{
    (void)arg;
    bool prev_state = button_is_pushed();
    while(1) {
        gpio_num_t gpio_num;
        xQueueReceive(button_gpio_event_queue, &gpio_num, portMAX_DELAY);
        int current_state = button_is_pushed();
        ESP_LOGI(TAG, "Button GPIO %d event: %s", gpio_num, current_state ? "closed" : "open");
        if( prev_state != current_state ) {
            if( sensor_endpoint != nullptr ) {
                const chip::EndpointId endpoint_id = esp_matter::endpoint::get_id(sensor_endpoint);
                lock::chip_stack_lock(portMAX_DELAY);
                chip::app::Clusters::BooleanState::Attributes::StateValue::Set(endpoint_id, current_state);
                lock::chip_stack_unlock();
            }
        }
        prev_state = current_state;
        // ESP_ERROR_CHECK(gpio_wakeup_enable(button_gpio_num, current_state ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL));
        // ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
        // if( !current_state ) {
        //     //button_setup_interrupt();
        //     gpio_num_t gpio_num;
        //     xQueueReceive(button_gpio_event_queue, &gpio_num, portMAX_DELAY);
        //     ESP_LOGI(TAG, "Button GPIO %d event: %s", gpio_num, current_state ? "closed" : "open");
        // } else {
        //     vTaskDelay(pdMS_TO_TICKS(10));
        // }
        // if( xQueueReceive(button_gpio_event_queue, &gpio_num, portMAX_DELAY) ) {
        //     if( sensor_endpoint != nullptr ) {
        //         const chip::EndpointId endpoint_id = esp_matter::endpoint::get_id(sensor_endpoint);
        //         bool closed = gpio_get_level(gpio_num) == 0;
        //         ESP_LOGI(TAG, "Button GPIO %d event: %s", gpio_num, closed ? "closed" : "open");
        //         lock::chip_stack_lock(portMAX_DELAY);
        //         chip::app::Clusters::BooleanState::Attributes::StateValue::Set(endpoint_id, closed);
        //         lock::chip_stack_unlock();
        //     }
        // }
    }

}

static esp_err_t IRAM_ATTR button_sleep_enter(int64_t sleep_time_us, void *arg)
{
    (void)arg;
    (void)sleep_time_us;
    // ESP_ERROR_CHECK(gpio_intr_disable(button_gpio_num));
    // ESP_ERROR_CHECK(gpio_wakeup_enable(button_gpio_num, GPIO_INTR_LOW_LEVEL));
    // ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    //ESP_ERROR_CHECK(rtc_gpio_wakeup_enable(button_gpio_num, GPIO_INTR_LOW_LEVEL));
    return ESP_OK;
}


static esp_err_t IRAM_ATTR button_sleep_exit(int64_t sleep_time_us, void *arg)
{
    (void)arg;
    (void)sleep_time_us;
    auto gpio_num = button_gpio_num;
    if( esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_ULP) {
        //ESP_ERROR_CHECK(rtc_gpio_wakeup_disable(button_gpio_num));
        if( button_gpio_event_queue != nullptr ) {
            BaseType_t higher_priority_task_woken = pdFALSE;
            xQueueSendFromISR(button_gpio_event_queue, &gpio_num, &higher_priority_task_woken);
        }
    }
    return ESP_OK;
}

static void lp_core_init(void)
{
    /* Set LP core wakeup source as the HP CPU */
    ulp_lp_core_cfg_t cfg = {
        .wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER,
        .lp_timer_sleep_duration_us = 1000000,  // 100[ms]
    };

    /* Load LP core firmware */
    ESP_ERROR_CHECK(ulp_lp_core_load_binary(lp_core_main_bin_start, (lp_core_main_bin_end - lp_core_main_bin_start)));

    /* Run LP core */
    ESP_ERROR_CHECK(ulp_lp_core_run(&cfg));

    ESP_LOGI(TAG, "LP core loaded with firmware and running successfully\n");
}

#if CONFIG_ESP_SLEEP_DEBUG
static esp_sleep_context_t s_sleep_ctx;

static void print_sleep_flag(void *arg)
{
    ESP_LOGI(TAG, "sleep_flags %lx", s_sleep_ctx.sleep_flags);
}
#endif

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    ESP_ERROR_CHECK(uart_set_pin((uart_port_t)0, 1, -1, -1, -1));
    ESP_LOGI(TAG, "Initializing OpenThread sleepy device example");

    /* Initialize the ESP NVS layer */
    nvs_flash_init();

#if CONFIG_PM_ENABLE
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true
#endif
    };
    err = esp_pm_configure(&pm_config);
#endif
    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    endpoint::on_off_light::config_t endpoint_config;
    endpoint_t *app_endpoint = endpoint::on_off_light::create(node, &endpoint_config, ENDPOINT_FLAG_NONE, NULL);

    /* These node and endpoint handles can be used to create/add other endpoints and clusters. */
    if (!node || !app_endpoint) {
        ESP_LOGE(TAG, "Matter node creation failed");
    }

    {
        endpoint::contact_sensor::config_t config;
        sensor_endpoint = endpoint::contact_sensor::create(node, &config, ENDPOINT_FLAG_NONE, nullptr);
        if( sensor_endpoint == nullptr ) {
            ESP_LOGI(TAG, "Contact sensor endpoint creation failed.");
        }
    }

    // Configure GPIO
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    button_gpio_event_queue = xQueueCreate(10, sizeof(gpio_num_t));
    {
        gpio_config_t config = {
            .pin_bit_mask = 1ULL << button_gpio_num,  // NanoC6 push button.
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&config));
        ESP_ERROR_CHECK(gpio_sleep_set_direction(button_gpio_num, GPIO_MODE_INPUT));
        ESP_ERROR_CHECK(gpio_sleep_sel_en(button_gpio_num));
        // ESP_ERROR_CHECK(gpio_wakeup_enable(button_gpio_num, GPIO_INTR_LOW_LEVEL));
        // ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
        ESP_ERROR_CHECK(gpio_isr_handler_add(button_gpio_num, button_gpio_isr_handler, (void *)button_gpio_num));
        button_setup_interrupt();
        
        esp_pm_sleep_cbs_register_config_t sleep_cbs = {
            .enter_cb = button_sleep_enter,
            .exit_cb = button_sleep_exit,
            .enter_cb_user_arg = nullptr,
            .exit_cb_user_arg = nullptr,
            .enter_cb_prior = 0,
            .exit_cb_prior = 0,
        };
        ESP_ERROR_CHECK(esp_pm_light_sleep_register_cbs(&sleep_cbs));
        ESP_ERROR_CHECK(rtc_gpio_init(button_gpio_num));
        ESP_ERROR_CHECK(rtc_gpio_set_direction(button_gpio_num, RTC_GPIO_MODE_INPUT_ONLY));
        ESP_ERROR_CHECK(rtc_gpio_pullup_en(button_gpio_num));
        ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(button_gpio_num));
        ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());
        lp_core_init();
        // ESP_ERROR_CHECK(rtc_gpio_wakeup_enable(button_gpio_num, GPIO_INTR_LOW_LEVEL));
        // ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup(1 << button_gpio_num, ESP_EXT1_WAKEUP_ANY_LOW));
        xTaskCreate(button_gpio_event_task, "button_gpio_event_task", 4096, nullptr, 10, nullptr);
    }
#if CONFIG_ESP_SLEEP_DEBUG
    esp_sleep_set_sleep_context(&s_sleep_ctx);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    // create a timer to print the status of sleepy device
    int periods = 2000;
    const esp_timer_create_args_t timer_args = {
            .callback = &print_sleep_flag,
            .arg  = NULL,
            .name = "print_sleep_flag",
            .skip_unhandled_events = true,
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, periods * 1000));
#endif

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
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Matter start failed: %d", err);
    }
}
