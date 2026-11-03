#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wol_send_magic_packet(const char *mac_str);

#ifdef __cplusplus
}
#endif
