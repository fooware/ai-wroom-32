#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start the LAN credential endpoint, or update its current IP and pairing PIN.
 * Accepted Cursor and Codex credentials are held in RAM only.
 */
esp_err_t credential_server_start(ui_attraction_t *ui, const char *ip_address,
                                  uint16_t pairing_pin);

typedef struct {
  char *cursor_cookie;
  char *codex_access_token;
  char *codex_account_id;
} credential_snapshot_t;

typedef void (*credential_listener_t)(bool present);

void credential_server_set_listener(credential_listener_t listener);
bool credential_server_has_credentials(void);
bool credential_server_snapshot(credential_snapshot_t *out);
void credential_snapshot_free(credential_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
