#include "nvs_config.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <string.h>

static const char *TAG = "nvs_config";
static const char *NVS_NAMESPACE = "smartpc_cfg";

esp_err_t nvs_config_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t nvs_config_load(app_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    memset(config, 0, sizeof(app_config_t));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS namespace '%s' not found (first run?)", NVS_NAMESPACE);
        config->configured = false;
        return err;
    }

    uint8_t conf_val = 0;
    nvs_get_u8(handle, "configured", &conf_val);
    config->configured = (conf_val == 1);

    size_t len = sizeof(config->wifi_ssid);
    nvs_get_str(handle, "wifi_ssid", config->wifi_ssid, &len);

    len = sizeof(config->wifi_password);
    nvs_get_str(handle, "wifi_pass", config->wifi_password, &len);

    len = sizeof(config->pc_mac);
    nvs_get_str(handle, "pc_mac", config->pc_mac, &len);

    len = sizeof(config->pc_ip);
    nvs_get_str(handle, "pc_ip", config->pc_ip, &len);

    nvs_close(handle);

    ESP_LOGI(TAG, "Loaded config: configured=%d, ssid='%s', mac='%s', ip='%s'",
             config->configured, config->wifi_ssid, config->pc_mac, config->pc_ip);
    return ESP_OK;
}

esp_err_t nvs_config_save(const app_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_u8(handle, "configured", config->configured ? 1 : 0);
    nvs_set_str(handle, "wifi_ssid", config->wifi_ssid);
    nvs_set_str(handle, "wifi_pass", config->wifi_password);
    nvs_set_str(handle, "pc_mac", config->pc_mac);
    nvs_set_str(handle, "pc_ip", config->pc_ip);

    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Configuration saved successfully (configured=%d)", config->configured);
    return err;
}

esp_err_t nvs_config_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_erase_all(handle);
    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "NVS configuration cleared to factory defaults");
    return err;
}
