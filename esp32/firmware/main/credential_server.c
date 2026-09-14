#include "credential_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define CREDENTIAL_URI "/api/credentials"
#define MAX_BODY_BYTES 16384
#define MAX_PIN_FAILURES 3
#define CREDENTIAL_TTL_SECONDS 3600
#define CREDENTIAL_TTL_US ((int64_t)CREDENTIAL_TTL_SECONDS * 1000 * 1000)

static const char *TAG = "credentials";

typedef struct {
  char *cursor_cookie;
  char *codex_access_token;
  char *codex_account_id;
} ram_credentials_t;

typedef struct {
  httpd_handle_t server;
  SemaphoreHandle_t lock;
  esp_timer_handle_t expiry_timer;
  ui_attraction_t *ui;
  ram_credentials_t credentials;
  char ip_address[16];
  uint16_t pairing_pin;
  uint8_t pin_failures;
  bool locked;
  credential_listener_t listener;
} credential_server_state_t;

static credential_server_state_t state;

static void wipe_and_free(char **value) {
  if (*value == NULL) {
    return;
  }
  volatile unsigned char *cursor = (volatile unsigned char *)*value;
  size_t length = strlen(*value);
  while (length-- > 0) {
    *cursor++ = 0;
  }
  free(*value);
  *value = NULL;
}

static void clear_credentials_locked(void) {
  wipe_and_free(&state.credentials.cursor_cookie);
  wipe_and_free(&state.credentials.codex_access_token);
  wipe_and_free(&state.credentials.codex_account_id);
}

static void notify_listener(bool present) {
  if (state.listener != NULL) {
    state.listener(present);
  }
}

static void expiry_task(void *arg) {
  (void)arg;
  xSemaphoreTake(state.lock, portMAX_DELAY);
  clear_credentials_locked();
  xSemaphoreGive(state.lock);
  ESP_LOGI(TAG, "Computer credential lease expired; returning to attraction mode");
  notify_listener(false);
  vTaskDelete(NULL);
}

static void expiry_timer_callback(void *arg) {
  (void)arg;
  /* The listener rebuilds both screens, so this task needs more than a hint. */
  if (xTaskCreate(expiry_task, "cred_expire", 5120, NULL, 5, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Could not start credential expiry task");
  }
}

static void send_json(httpd_req_t *request, const char *status, const char *body) {
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "application/json");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_sendstr(request, body);
}

static bool valid_pin(httpd_req_t *request) {
  char authorization[32] = {0};
  if (httpd_req_get_hdr_value_str(request, "Authorization", authorization,
                                  sizeof(authorization)) != ESP_OK) {
    return false;
  }

  char expected[20];
  snprintf(expected, sizeof(expected), "Bearer %04u",
           (unsigned)(state.pairing_pin % 10000));
  size_t actual_len = strnlen(authorization, sizeof(authorization));
  size_t expected_len = strlen(expected);
  unsigned char difference = (unsigned char)(actual_len ^ expected_len);
  for (size_t i = 0; i < expected_len; ++i) {
    unsigned char actual = i < actual_len ? (unsigned char)authorization[i] : 0;
    difference |= actual ^ (unsigned char)expected[i];
  }
  return difference == 0;
}

static bool duplicate_json_string(cJSON *parent, const char *name, char **out) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, name);
  if (!cJSON_IsString(item) || item->valuestring == NULL ||
      item->valuestring[0] == '\0') {
    return false;
  }
  *out = strdup(item->valuestring);
  return *out != NULL;
}

static bool parse_credentials(const char *body, ram_credentials_t *credentials) {
  cJSON *root = cJSON_Parse(body);
  if (root == NULL) {
    return false;
  }

  cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
  cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
  cJSON *codex = cJSON_GetObjectItemCaseSensitive(root, "codex");
  bool valid = cJSON_IsNumber(version) && version->valueint == 1 &&
               cJSON_IsObject(cursor) && cJSON_IsObject(codex) &&
               duplicate_json_string(cursor, "cookie",
                                     &credentials->cursor_cookie) &&
               duplicate_json_string(codex, "access_token",
                                     &credentials->codex_access_token) &&
               duplicate_json_string(codex, "account_id",
                                     &credentials->codex_account_id);
  cJSON_Delete(root);
  if (!valid) {
    wipe_and_free(&credentials->cursor_cookie);
    wipe_and_free(&credentials->codex_access_token);
    wipe_and_free(&credentials->codex_account_id);
  }
  return valid;
}

static esp_err_t credentials_post(httpd_req_t *request) {
  xSemaphoreTake(state.lock, portMAX_DELAY);
  bool locked = state.locked;
  xSemaphoreGive(state.lock);
  if (locked) {
    send_json(request, "423 Locked", "{\"ok\":false,\"error\":\"locked\"}");
    return ESP_OK;
  }

  if (!valid_pin(request)) {
    xSemaphoreTake(state.lock, portMAX_DELAY);
    state.pin_failures++;
    if (state.pin_failures >= MAX_PIN_FAILURES) {
      state.locked = true;
    }
    uint8_t failures = state.pin_failures;
    locked = state.locked;
    xSemaphoreGive(state.lock);
    ESP_LOGW(TAG, "Incorrect pairing PIN (%u/%u)", failures, MAX_PIN_FAILURES);
    if (locked) {
      send_json(request, "423 Locked",
                "{\"ok\":false,\"error\":\"locked_until_reboot\"}");
    } else {
      send_json(request, "401 Unauthorized",
                "{\"ok\":false,\"error\":\"incorrect_pin\"}");
    }
    return ESP_OK;
  }

  if (request->content_len <= 0 || request->content_len > MAX_BODY_BYTES) {
    send_json(request, "413 Payload Too Large",
              "{\"ok\":false,\"error\":\"invalid_size\"}");
    return ESP_OK;
  }

  char *body = malloc((size_t)request->content_len + 1);
  if (body == NULL) {
    send_json(request, "500 Internal Server Error",
              "{\"ok\":false,\"error\":\"out_of_memory\"}");
    return ESP_OK;
  }

  int received = 0;
  while (received < request->content_len) {
    int result = httpd_req_recv(request, body + received,
                                request->content_len - received);
    if (result <= 0) {
      free(body);
      return result == HTTPD_SOCK_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
    }
    received += result;
  }
  body[received] = '\0';

  ram_credentials_t incoming = {0};
  bool valid = parse_credentials(body, &incoming);
  memset(body, 0, (size_t)received);
  free(body);
  if (!valid) {
    send_json(request, "400 Bad Request",
              "{\"ok\":false,\"error\":\"invalid_payload\"}");
    return ESP_OK;
  }

  xSemaphoreTake(state.lock, portMAX_DELAY);
  clear_credentials_locked();
  state.credentials = incoming;
  state.pin_failures = 0;
  xSemaphoreGive(state.lock);

  esp_timer_stop(state.expiry_timer);
  esp_timer_start_once(state.expiry_timer, CREDENTIAL_TTL_US);
  ESP_LOGI(TAG, "Accepted RAM-only credentials; lease refreshed for one hour");
  notify_listener(true);
  char response[48];
  snprintf(response, sizeof(response), "{\"ok\":true,\"lease_seconds\":%d}",
           CREDENTIAL_TTL_SECONDS);
  send_json(request, "200 OK", response);
  return ESP_OK;
}

esp_err_t credential_server_start(ui_attraction_t *ui, const char *ip_address,
                                  uint16_t pairing_pin) {
  state.ui = ui;
  state.pairing_pin = pairing_pin;
  strlcpy(state.ip_address, ip_address, sizeof(state.ip_address));

  if (state.server != NULL) {
    return ESP_OK;
  }
  state.lock = xSemaphoreCreateMutex();
  if (state.lock == NULL) {
    return ESP_ERR_NO_MEM;
  }

  const esp_timer_create_args_t timer_args = {
      .callback = expiry_timer_callback,
      .name = "cred_ttl",
  };
  esp_err_t err = esp_timer_create(&timer_args, &state.expiry_timer);
  if (err != ESP_OK) {
    vSemaphoreDelete(state.lock);
    state.lock = NULL;
    return err;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 4;
  config.stack_size = 6144;
  err = httpd_start(&state.server, &config);
  if (err != ESP_OK) {
    esp_timer_delete(state.expiry_timer);
    state.expiry_timer = NULL;
    vSemaphoreDelete(state.lock);
    state.lock = NULL;
    return err;
  }

  const httpd_uri_t endpoint = {
      .uri = CREDENTIAL_URI,
      .method = HTTP_POST,
      .handler = credentials_post,
  };
  err = httpd_register_uri_handler(state.server, &endpoint);
  if (err != ESP_OK) {
    httpd_stop(state.server);
    state.server = NULL;
    return err;
  }
  ESP_LOGI(TAG, "Credential endpoint ready at http://%s%s", state.ip_address,
           CREDENTIAL_URI);
  return ESP_OK;
}

void credential_server_set_listener(credential_listener_t listener) {
  state.listener = listener;
}

bool credential_server_has_credentials(void) {
  if (state.lock == NULL) {
    return false;
  }
  xSemaphoreTake(state.lock, portMAX_DELAY);
  bool present = state.credentials.cursor_cookie != NULL &&
                 state.credentials.codex_access_token != NULL;
  xSemaphoreGive(state.lock);
  return present;
}

bool credential_server_snapshot(credential_snapshot_t *out) {
  if (out == NULL || state.lock == NULL) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  xSemaphoreTake(state.lock, portMAX_DELAY);
  bool ok = state.credentials.cursor_cookie != NULL &&
            state.credentials.codex_access_token != NULL &&
            state.credentials.codex_account_id != NULL;
  if (ok) {
    out->cursor_cookie = strdup(state.credentials.cursor_cookie);
    out->codex_access_token = strdup(state.credentials.codex_access_token);
    out->codex_account_id = strdup(state.credentials.codex_account_id);
    ok = out->cursor_cookie != NULL && out->codex_access_token != NULL &&
         out->codex_account_id != NULL;
  }
  xSemaphoreGive(state.lock);
  if (!ok) {
    credential_snapshot_free(out);
  }
  return ok;
}

void credential_snapshot_free(credential_snapshot_t *snapshot) {
  if (snapshot == NULL) {
    return;
  }
  wipe_and_free(&snapshot->cursor_cookie);
  wipe_and_free(&snapshot->codex_access_token);
  wipe_and_free(&snapshot->codex_account_id);
}
