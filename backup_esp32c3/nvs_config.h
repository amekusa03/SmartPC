#pragma once

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    char pc_mac[18];
    char pc_ip[16];
    bool configured;
} app_config_t;

esp_err_t nvs_config_init(void);
esp_err_t nvs_config_load(app_config_t *config);
esp_err_t nvs_config_save(const app_config_t *config);
esp_err_t nvs_config_clear(void);

#ifdef __cplusplus
}
#endif
