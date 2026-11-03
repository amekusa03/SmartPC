#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize TinyUSB HID for PC Control (Keyboard / Consumer Control)
 */
esp_err_t usb_hid_pc_init(void);

/**
 * @brief Send PC Wake-Up command via USB HID
 */
void usb_hid_pc_wake(void);

/**
 * @brief Send PC Sleep command via USB HID (System Sleep / Power Down)
 */
void usb_hid_pc_sleep(void);

/**
 * @brief Check if USB is mounted / PC is ON
 */
bool usb_hid_pc_is_connected(void);

/**
 * @brief Check if USB bus is in suspend state (PC is Sleeping/Off)
 */
bool usb_hid_pc_is_suspended(void);

#ifdef __cplusplus
}
#endif
