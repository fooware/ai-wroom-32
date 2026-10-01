#include "usage_poll.h"
#include "credential_server.h"
#include "provider_parser.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POLL_PERIOD_MS 15000
#define INITIAL_RESPONSE_BYTES 4096
#define MAX_RESPONSE_BYTES 24576
#define MASK(id) (1u << (unsigned)(id))

static const char *TAG = "usage";
static TaskHandle_t poll_task;
static usage_update_cb_t on_update;
static usage_attraction_cb_t on_attraction;
static uint32_t enabled_mask = (1u << PROVIDER_COUNT) - 1;

typedef struct { const char *url; provider_parse_fn_t parse; } provider_adapter_t;
/* Endpoint URLs are code-owned. Credential payloads cannot redirect tokens. */
static const provider_adapter_t adapters[PROVIDER_COUNT] = {
  [PROVIDER_CODEX] = {"https://chatgpt.com/backend-api/wham/usage", provider_parse_codex},
  [PROVIDER_CURSOR] = {"https://cursor.com/api/usage-summary", provider_parse_cursor},
  [PROVIDER_CLAUDE] = {"https://api.anthropic.com/api/oauth/usage", provider_parse_claude},
};

static void wipe(void *value, size_t length) {
  volatile unsigned char *p = value;
  while (length--) *p++ = 0;
}

static esp_err_t https_get(const char *url, const char *headers[][2], size_t header_count,
                           char **body, int *body_len, int *status_code) {
  *body = NULL;
  *body_len = 0;
  *status_code = 0;
  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_GET,
      .timeout_ms = 15000,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .buffer_size = 2048,
      /* Session cookie and bearer token exceed the default header buffer. */
      .buffer_size_tx = 4096,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    return ESP_ERR_NO_MEM;
  }
  for (size_t i = 0; i < header_count; ++i) {
    esp_http_client_set_header(client, headers[i][0], headers[i][1]);
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    esp_http_client_cleanup(client);
    return err;
  }
  if (esp_http_client_fetch_headers(client) < 0) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }
  int status = esp_http_client_get_status_code(client);
  *status_code = status;
  if (status != 200) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }

  /* Grow on demand: a TLS session plus a worst-case buffer does not fit at once. */
  int capacity = INITIAL_RESPONSE_BYTES;
  char *buffer = malloc(capacity);
  if (buffer == NULL) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_ERR_NO_MEM;
  }
  int received = 0;
  while (true) {
    if (received == capacity - 1) {
      if (capacity >= MAX_RESPONSE_BYTES) {
        if (esp_http_client_is_complete_data_received(client)) break;
        free(buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
      }
      int wanted = capacity * 2;
      char *grown = realloc(buffer, wanted > MAX_RESPONSE_BYTES ? MAX_RESPONSE_BYTES : wanted);
      if (grown == NULL) {
        free(buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
      }
      buffer = grown;
      capacity = wanted > MAX_RESPONSE_BYTES ? MAX_RESPONSE_BYTES : wanted;
    }
    int chunk = esp_http_client_read(client, buffer + received, capacity - 1 - received);
    if (chunk < 0) {
      free(buffer);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return ESP_FAIL;
    }
    if (chunk == 0) {
      break;
    }
    received += chunk;
  }
  if (!esp_http_client_is_complete_data_received(client)) {
    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_ERR_INVALID_RESPONSE;
  }
  buffer[received] = '\0';
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  *body = buffer;
  *body_len = received;
  return ESP_OK;
}

static void fetch_provider(provider_id_t id, const credential_snapshot_t *snapshot, provider_data_t *out) {
  *out = (provider_data_t){.remaining = {-1, -1}};
  const char *headers[5][2] = {{"Accept", "application/json"}, {"User-Agent", "ai-wroom-32/0.2"}};
  size_t count = 2;
  char *authorization = NULL;
  if (id == PROVIDER_CURSOR) {
    headers[count][0] = "Cookie"; headers[count++][1] = snapshot->cursor_cookie;
    headers[count][0] = "Origin"; headers[count++][1] = "https://cursor.com";
  } else {
    const char *token = id == PROVIDER_CODEX ? snapshot->codex_access_token : snapshot->claude_access_token;
    size_t length = strlen(token) + 8;
    authorization = malloc(length);
    if (!authorization) { snprintf(out->status, sizeof(out->status), "Low memory"); return; }
    snprintf(authorization, length, "Bearer %s", token);
    headers[count][0] = "Authorization"; headers[count++][1] = authorization;
    if (id == PROVIDER_CODEX) {
      headers[count][0] = "ChatGPT-Account-Id"; headers[count++][1] = snapshot->codex_account_id;
    } else {
      headers[count][0] = "anthropic-beta"; headers[count++][1] = "oauth-2025-04-20";
    }
  }
  char *body = NULL;
  int length = 0, status = 0;
  esp_err_t err = https_get(adapters[id].url, headers, count, &body, &length, &status);
  if (authorization) { wipe(authorization, strlen(authorization)); free(authorization); }
  bool parsed = err == ESP_OK && adapters[id].parse(body, out);
  if (body) { wipe(body, length); free(body); }
  if (parsed) return;
  out->available = false;
  out->remaining[0] = out->remaining[1] = -1;
  if (err == ESP_ERR_NO_MEM) snprintf(out->status, sizeof(out->status), "Low memory");
  else if (status == 401 || status == 403) snprintf(out->status, sizeof(out->status), "Refresh login (%d)", status);
  else if (status && status != 200) snprintf(out->status, sizeof(out->status), "HTTP %d", status);
  else if (err == ESP_OK) snprintf(out->status, sizeof(out->status), "Usage unavailable");
  else snprintf(out->status, sizeof(out->status), "Connection failed");
  ESP_LOGW(TAG, "%s: %s", provider_info(id)->id, out->status);
}

/* Called from credential intake/expiry; all display callbacks run on poll_task. */
static void on_credentials(uint32_t available) {
  (void)available;
  if (poll_task) xTaskNotifyGive(poll_task);
}

static void poll_loop(void *arg) {
  (void)arg;
  bool showing = false;
  while (true) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(POLL_PERIOD_MS));
    if (!(credential_server_available_mask() & enabled_mask)) {
      if (showing && on_attraction) on_attraction();
      showing = false;
      continue;
    }
    for (unsigned i = 0; i < PROVIDER_COUNT; ++i) {
      if (!(enabled_mask & MASK(i))) continue;
      credential_snapshot_t snapshot = {0};
      if (!credential_server_snapshot(&snapshot)) break;
      provider_data_t data = {.remaining = {-1, -1}};
      if (snapshot.available_mask & MASK(i)) fetch_provider((provider_id_t)i, &snapshot, &data);
      else snprintf(data.status, sizeof(data.status), "Waiting for login");
      /* A superseded/expired credential set must not publish in-flight results. */
      if (credential_server_snapshot_current(snapshot.generation, 0)) {
        if (on_update) on_update((provider_id_t)i, &data);
        showing = true;
      }
      credential_snapshot_free(&snapshot);
    }
    if (!(credential_server_available_mask() & enabled_mask)) {
      if (showing && on_attraction) on_attraction();
      showing = false;
    }
  }
}

void usage_poll_set_enabled(uint32_t provider_mask) {
  /* Set once before usage_poll_start, using the active screen assignments. */
  enabled_mask = provider_mask & ((1u << PROVIDER_COUNT) - 1);
}

esp_err_t usage_poll_start(usage_update_cb_t update_cb, usage_attraction_cb_t attraction_cb) {
  on_update = update_cb;
  on_attraction = attraction_cb;
  credential_server_set_listener(on_credentials);
  if (xTaskCreate(poll_loop, "usage", 16384, NULL, 5, &poll_task) != pdPASS) return ESP_ERR_NO_MEM;
  return ESP_OK;
}
