#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_portal_start(void);
void      web_portal_stop(void);

#ifdef __cplusplus
}
#endif
