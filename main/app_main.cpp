#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_matter.h>
#include <stdio.h>
#include <string.h>

#include "usb_hid_pc.h"

static const char *TAG = "SmartPC";

#define FACTORY_RESET_GPIO         GPIO_NUM_0
#define FACTORY_RESET_HOLD_MS      5000
#define BUTTON_POLL_INTERVAL_MS    100

using namespace esp_matter;
using namespace esp_matter::endpoint;

static uint16_t pc_endpoint_id = 0;
static bool current_matter_on_state = false;
static bool user_action_in_progress = false;

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

// Matter の OnOff 状態をハードウェア状態と同期する
static void update_matter_pc_power_state(bool is_on)
{
    if (pc_endpoint_id == 0) return;

    uint32_t cluster_id = chip::app::Clusters::OnOff::Id;
    uint32_t attribute_id = chip::app::Clusters::OnOff::Attributes::OnOff::Id;
    attribute_t *attr = attribute::get(pc_endpoint_id, cluster_id, attribute_id);
    if (!attr) return;

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute::get_val(attr, &val);

    if (val.val.b != is_on) {
        ESP_LOGI(TAG, "Syncing Matter OnOff state -> %s (USB Hardware State)", is_on ? "ON" : "OFF");
        val.val.b = is_on;
        current_matter_on_state = is_on;
        attribute::update(pc_endpoint_id, cluster_id, attribute_id, &val);
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
                if (current_matter_on_state != on) {
                    ESP_LOGI(TAG, "Matter command received: Turn %s", on ? "ON" : "OFF");
                    current_matter_on_state = on;
                    user_action_in_progress = true;
                    execute_pc_action(on);
                }
            }
        }
    }
    return ESP_OK;
}

// USB状態監視＆Matter同期タスク（PCアプリ不要・ハードウェア生死監視）
static void pc_power_monitor_task(void *pvParameters)
{
    int consecutive_same_state_count = 0;
    bool last_sampled_state = false;
    bool confirmed_pc_on = false;

    // 起動時のUSB列挙安定待ち
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "PC Hardware Power Monitor started (Native USB suspend detection)");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));

        // Matterアプリ操作直後は過渡状態（スリープ/復帰途中）のため安定化を待つ
        if (user_action_in_progress) {
            vTaskDelay(pdMS_TO_TICKS(3000));
            user_action_in_progress = false;
            consecutive_same_state_count = 0;
            continue;
        }

        bool mounted = usb_hid_pc_is_connected();
        bool suspended = usb_hid_pc_is_suspended();

        // PC稼働中 = USBがマウントされており、かつサスペンド状態でない
        bool current_sample_on = (mounted && !suspended);

        if (current_sample_on == last_sampled_state) {
            consecutive_same_state_count++;
            // 3回連続（1.5秒）同じ状態が継続したら確定（チャタリング防止）
            if (consecutive_same_state_count >= 3) {
                if (confirmed_pc_on != current_sample_on) {
                    confirmed_pc_on = current_sample_on;
                    ESP_LOGI(TAG, "PC Power State Changed: %s (mounted=%d, suspended=%d)",
                             confirmed_pc_on ? "RUNNING (ON)" : "SLEEP/OFF (OFF)", mounted, suspended);
                    update_matter_pc_power_state(confirmed_pc_on);
                }
            }
        } else {
            last_sampled_state = current_sample_on;
            consecutive_same_state_count = 1;
        }
    }
}

// BOOTボタン（GPIO 0）長押しによるファクトリーリセット（NVS消去）タスク
static void factory_reset_button_task(void *pvParameters)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << FACTORY_RESET_GPIO);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    uint32_t hold_time_ms = 0;
    int last_reported_sec = 0;

    ESP_LOGI(TAG, "Factory reset button monitor started on GPIO %d (Hold for 5s to reset NVS)", FACTORY_RESET_GPIO);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_INTERVAL_MS));

        // BOOTボタンは押下時に LOW (0)
        if (gpio_get_level(FACTORY_RESET_GPIO) == 0) {
            hold_time_ms += BUTTON_POLL_INTERVAL_MS;
            int current_sec = hold_time_ms / 1000;

            if (current_sec > last_reported_sec && current_sec < (FACTORY_RESET_HOLD_MS / 1000)) {
                ESP_LOGW(TAG, "BOOT button held: %d/%d sec... (Release to cancel)", current_sec, FACTORY_RESET_HOLD_MS / 1000);
                last_reported_sec = current_sec;
            }

            if (hold_time_ms >= FACTORY_RESET_HOLD_MS) {
                ESP_LOGW(TAG, "==========================================================");
                ESP_LOGW(TAG, "  FACTORY RESET TRIGGERED! Erasing NVS and restarting...  ");
                ESP_LOGW(TAG, "==========================================================");
                printf("\n>>> FACTORY_RESET_START <<<\n");
                fflush(stdout);

                // 1. Matterスタック公式のファクトリーリセット
                esp_matter::factory_reset();

                // 2. 念のためNVS全体を確実に消去
                vTaskDelay(pdMS_TO_TICKS(1000));
                ESP_LOGI(TAG, "Erasing entire NVS flash...");
                nvs_flash_erase();

                ESP_LOGI(TAG, "Restarting ESP32-S3...");
                esp_restart();
            }
        } else {
            if (hold_time_ms > 0 && hold_time_ms < FACTORY_RESET_HOLD_MS) {
                ESP_LOGI(TAG, "BOOT button released early (%lu ms). Factory reset cancelled.", (unsigned long)hold_time_ms);
            }
            hold_time_ms = 0;
            last_reported_sec = 0;
        }
    }
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

    // BOOTボタン監視タスクの起動（5秒長押しでNVS初期化・ファクトリーリセット）
    xTaskCreate(factory_reset_button_task, "reset_btn_task", 4096, NULL, 5, NULL);

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
    pc_endpoint_id = endpoint::get_id(endpoint);

    // Matter スタックの起動
    err = esp_matter::start(NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter, err:%d", err);
        return;
    }

    // USBハードウェア生死監視タスク起動（PCアプリ不要でMatter状態を自動同期）
    xTaskCreate(pc_power_monitor_task, "pc_pwr_monitor", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "SmartPC Matter node successfully initialized!");
    ESP_LOGI(TAG, "Ready for commissioning & PC control.");
    ESP_LOGI(TAG, "Hold BOOT button (GPIO 0) for 5s to Factory Reset (Erase NVS).");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        bool mounted = usb_hid_pc_is_connected();
        bool suspended = usb_hid_pc_is_suspended();
        ESP_LOGI(TAG, "SmartPC Heartbeat: USB mounted=%d, suspended=%d, MatterOn=%d",
                 mounted, suspended, current_matter_on_state);
    }
}
