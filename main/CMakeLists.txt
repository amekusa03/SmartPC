idf_component_register(
    SRC_DIRS "."
    PRIV_INCLUDE_DIRS "." "${ESP_MATTER_PATH}/examples/common/utils" "${ESP_MATTER_PATH}/examples/common/app_reset"
    PRIV_REQUIRES esp_matter chip app_reset nvs_flash esp_wifi esp_netif esp_http_server lwip esp_driver_gpio espressif__esp_tinyusb espressif__tinyusb
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
