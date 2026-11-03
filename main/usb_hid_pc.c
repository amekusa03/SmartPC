#include "usb_hid_pc.h"
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "tusb_console.h"
#include "class/hid/hid_device.h"

static const char *TAG = "usb_hid_pc";
static bool s_is_suspended = false;

/************* TinyUSB Descriptors (CDC + HID) ****************/
enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_HID,
    ITF_NUM_TOTAL
};

enum {
    EPNUM_DEFAULT = 0,
    EPNUM_CDC_NOTIF = 1,
    EPNUM_CDC_DATA = 2,
    EPNUM_HID = 3
};

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)

const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD)),
    TUD_HID_REPORT_DESC_SYSTEM_CONTROL(HID_REPORT_ID(2))
};

const char* hid_string_descriptor[6] = {
    (char[]){0x09, 0x04},        // 0: Supported language: English (0x0409)
    "Espressif",                 // 1: Manufacturer
    "SmartPC Remote Control",    // 2: Product
    "ESP32S3-SMARTPC",           // 3: Serial
    "SmartPC CDC Console",       // 4: CDC Interface
    "SmartPC HID Interface",     // 5: HID Interface
};

static const uint8_t hid_configuration_descriptor[] = {
    // Configuration descriptor: 1 Config, 3 Interfaces (CDC x 2 + HID x 1)
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // CDC Descriptor: Interface ITF_NUM_CDC & ITF_NUM_CDC_DATA
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, 0x80 | EPNUM_CDC_NOTIF, 8, EPNUM_CDC_DATA, 0x80 | EPNUM_CDC_DATA, 64),

    // HID Descriptor: Interface ITF_NUM_HID
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 5, false, sizeof(hid_report_descriptor), 0x80 | EPNUM_HID, 16, 10),
};

/********* TinyUSB Callbacks ***************/

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

// Suspend callback (PC went to sleep / port turned off)
void tud_suspend_cb(bool remote_wakeup_en)
{
    s_is_suspended = true;
    ESP_LOGI(TAG, "USB Suspend detected (PC sleeping/off), remote_wakeup=%d", remote_wakeup_en);
}

// Resume callback (PC woke up)
void tud_resume_cb(void)
{
    s_is_suspended = false;
    ESP_LOGI(TAG, "USB Resume detected (PC awake)");
}

/********* Public Control Functions ***************/

esp_err_t usb_hid_pc_init(void)
{
    ESP_LOGI(TAG, "Initializing TinyUSB CDC + HID Composite...");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = hid_string_descriptor,
        .string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = hid_configuration_descriptor,
        .hs_configuration_descriptor = hid_configuration_descriptor,
        .qualifier_descriptor = NULL,
#else
        .configuration_descriptor = hid_configuration_descriptor,
#endif
    };

    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    // Initialize TinyUSB CDC Console for stdout / logging redirection
    esp_tusb_init_console(TINYUSB_CDC_ACM_0);

    ESP_LOGI(TAG, "TinyUSB CDC + HID initialized successfully");
    return ESP_OK;
}

bool usb_hid_pc_is_connected(void)
{
    return tud_mounted();
}

bool usb_hid_pc_is_suspended(void)
{
    return s_is_suspended;
}

void usb_hid_pc_wake(void)
{
    ESP_LOGI(TAG, "Executing PC Wake action...");
    if (tud_suspended()) {
        ESP_LOGI(TAG, "Triggering USB Remote Wakeup...");
        tud_remote_wakeup();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (tud_mounted()) {
        ESP_LOGI(TAG, "Sending Keyboard SPACE key report...");
        uint8_t keycode[6] = {HID_KEY_SPACE, 0, 0, 0, 0, 0};
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycode);
        vTaskDelay(pdMS_TO_TICKS(50));
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
        vTaskDelay(pdMS_TO_TICKS(50));
    } else {
        ESP_LOGW(TAG, "USB not mounted, cannot send HID key report");
    }
}

void usb_hid_pc_sleep(void)
{
    ESP_LOGI(TAG, "Executing PC Sleep action (HID System Control: Standby/Sleep)...");
    if (tud_mounted()) {
        // System Control: 0x01 = Power Off, 0x02 = Standby/Sleep, 0x03 = Wake Host
        uint8_t system_key = 2; // 0x02: Standby / Sleep
        tud_hid_report(2, &system_key, sizeof(system_key));
        vTaskDelay(pdMS_TO_TICKS(100));

        system_key = 0;
        tud_hid_report(2, &system_key, sizeof(system_key));
        vTaskDelay(pdMS_TO_TICKS(50));
    } else {
        ESP_LOGW(TAG, "USB not mounted, cannot send HID sleep report");
    }
}
