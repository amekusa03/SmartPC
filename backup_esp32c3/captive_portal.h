#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t captive_portal_start(void);
void captive_portal_stop(void);

#ifdef __cplusplus
}
#endif
