#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_matter.h>
#include <esp_matter_console.h>
#include <stdio.h>
#include <string.h>

#include "usb_hid_pc.h"

static const char *TAG = "SmartPC";

using namespace esp_matter;
using namespace esp_matter::endpoint;

// PC制御用コマンド実行（USB HID + シリアル）
static void execute_pc_action(bool on)
{
    if (on) {
        ESP_LOGI(TAG, "=== Action: PC WAKE ===");
        printf("\n>>> CMD:WAKE <<<\n");
        fflush(stdout);
        usb_hid_pc_wake();
    } else {
        ESP_LOGI(TAG, "=== Action: PC SLEEP ===");
        printf("\n>>> CMD:LOCK <<<\n");
        fflush(stdout);
        usb_hid_pc_sleep();
    }
}

// Matter クラスター更新コールバック
static esp_err_t app_attribute_update_cb(
    attribute::callback_type_t type,
    uint16_t endpoint_id,
    uint32_t cluster_id,
    uint32_t attribute_id,
    esp_matter_attr_val_t *val,
    void *priv_data)
{
    if (type == attribute::PRE_UPDATE) {
        if (cluster_id == chip::app::Clusters::OnOff::Id) {
            if (attribute_id == chip::app::Clusters::OnOff::Attributes::OnOff::Id) {
                bool on = val->val.b;
                ESP_LOGI(TAG, "Matter OnOff state changed to: %s", on ? "ON" : "OFF");
                execute_pc_action(on);
            }
        }
    }
    return ESP_OK;
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "  SmartPC Matter Node (USB HID + CDC Control)  ");
    ESP_LOGI(TAG, "===============================================");

    // NVSの初期化
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // USB HID + CDC 初期化
    err = usb_hid_pc_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB HID/CDC: %d", err);
    } else {
        ESP_LOGI(TAG, "USB HID/CDC initialized successfully!");
    }

    // Matter ノードの作成
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, NULL);
    if (!node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return;
    }

    // On/Off Plug-in Unit (スマートプラグ/スイッチ) エンドポイントの作成
    on_off_plug_in_unit::config_t endpoint_config;
    endpoint_t *endpoint = on_off_plug_in_unit::create(node, &endpoint_config, ENDPOINT_FLAG_NONE, NULL);
    if (!endpoint) {
        ESP_LOGE(TAG, "Failed to create Matter endpoint");
        return;
    }

    // Matter スタックの起動
    err = esp_matter::start(NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter, err:%d", err);
        return;
    }

    ESP_LOGI(TAG, "SmartPC Matter node successfully initialized!");
    ESP_LOGI(TAG, "Ready for commissioning & PC control.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        bool mounted = usb_hid_pc_is_connected();
        bool suspended = usb_hid_pc_is_suspended();
        ESP_LOGI(TAG, "SmartPC Heartbeat: USB mounted=%d, suspended=%d", mounted, suspended);
    }
}
