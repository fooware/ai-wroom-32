#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ui.h"
#include "provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start the LAN credential endpoint, or update its current IP and pairing PIN.
 * Accepted provider credentials are held in RAM only.
 */
esp_err_t credential_server_start(ui_attraction_t *ui, const char *ip_address,
                                  uint16_t pairing_pin);

typedef struct {
  char *cursor_cookie;
  char *codex_access_token;
  char *codex_account_id;
  char *claude_access_token;
  uint32_t generation;
  uint32_t available_mask;
} credential_snapshot_t;

typedef void (*credential_listener_t)(uint32_t available_mask);

void credential_server_set_listener(credential_listener_t listener);
uint32_t credential_server_available_mask(void);
bool credential_server_snapshot(credential_snapshot_t *out);
bool credential_server_snapshot_current(uint32_t generation, uint32_t provider_mask);
void credential_snapshot_free(credential_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
