#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start Wi-Fi from credentials in NVS, or start secure SoftAP provisioning
 * when the device has not been provisioned. The provisioning PIN is boot-only.
 */
esp_err_t network_start(ui_attraction_t *ui, uint16_t provisioning_pin);

/* Boot-scoped PIN used by the later RAM-only credential server. */
uint16_t network_pairing_pin(void);
bool network_is_connected(void);
const char *network_sta_ip(void);
void network_set_attraction(ui_attraction_t *ui);

#ifdef __cplusplus
}
#endif
