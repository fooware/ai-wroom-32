#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "provider.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef void (*usage_update_cb_t)(provider_id_t id, const provider_data_t *data);
typedef void (*usage_attraction_cb_t)(void);
esp_err_t usage_poll_start(usage_update_cb_t update_cb, usage_attraction_cb_t attraction_cb);
void usage_poll_set_enabled(uint32_t provider_mask);
#ifdef __cplusplus
}
#endif
