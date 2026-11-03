#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "nvs_config.h"
#include "captive_portal.h"
#include "wol.h"

static const char *TAG = "smartpc_main";

#define BOOT_BUTTON_GPIO GPIO_NUM_9

static app_config_t s_app_config;
static bool s_wifi_connected = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi Station started, connecting to '%s'...", s_app_config.wifi_ssid);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected, retrying connection in 3s...");
        s_wifi_connected = false;
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "★ Wi-Fi Connected! IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
    }
}

static void start_station_mode(const app_config_t *cfg)
{
    ESP_LOGI(TAG, "Starting Wi-Fi in Station mode for SSID: %s", cfg->wifi_ssid);

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, cfg->wifi_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, cfg->wifi_pass, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// Button monitor task:
// - Long press (>4s) at runtime: Reset NVS & Launch Captive Portal
// - Short press (<1s): Send test WOL packet to PC
static void button_task(void *pvParameters)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    while (1) {
        if (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
            int pressed_ms = 0;
            while (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
                vTaskDelay(pdMS_TO_TICKS(100));
                pressed_ms += 100;
            }

            if (pressed_ms >= 4000) {
                ESP_LOGW(TAG, "BOOT button held for %d ms: Resetting NVS settings!", pressed_ms);
                nvs_config_clear();
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            } else if (pressed_ms > 50) {
                ESP_LOGI(TAG, "BOOT button pressed (%d ms): Triggering manual WoL packet!", pressed_ms);
                if (s_wifi_connected && s_app_config.configured) {
                    wol_send_magic_packet(s_app_config.pc_mac);
                } else {
                    ESP_LOGW(TAG, "Cannot send WoL: Wi-Fi not connected or PC MAC unconfigured");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== SmartPC on ESP32-C3 Booting ===");

    // 1. Initialize NVS
    ESP_ERROR_CHECK(nvs_config_init());

    // 2. Initialize TCP/IP and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // 3. Start button listener task (for test WOL and Reset)
    xTaskCreate(button_task, "btn_task", 2048, NULL, 5, NULL);

    // 4. Load configuration from NVS
    esp_err_t err = nvs_config_load(&s_app_config);

    if (err != ESP_OK || !s_app_config.configured) {
        ESP_LOGI(TAG, "No valid Wi-Fi / PC configuration found.");
        ESP_LOGI(TAG, "Entering Captive Portal Setup Mode...");
        captive_portal_start();
    } else {
        ESP_LOGI(TAG, "Configured Wi-Fi found: SSID='%s', Target PC MAC='%s'",
                 s_app_config.wifi_ssid, s_app_config.pc_mac);
        start_station_mode(&s_app_config);
    }
}
