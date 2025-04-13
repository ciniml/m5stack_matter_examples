idf_component_register(SRC_DIRS          "." "drivers"
                       PRIV_INCLUDE_DIRS "." "drivers" "${ESP_MATTER_PATH}/examples/common/utils"
                       REQUIRES M5GFX M5Unified esp_matter esp_matter_console esp_matter_ota_provider esp_matter_controller app_reset)

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
