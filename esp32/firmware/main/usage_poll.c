#include "usage_poll.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "credential_server.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CURSOR_USAGE_URL "https://cursor.com/api/usage-summary"
#define CODEX_USAGE_URL "https://chatgpt.com/backend-api/wham/usage"
/* Idle wait after each fetch (or until a credential push wakes the task). */
#define POLL_PERIOD_MS 15000
#define INITIAL_RESPONSE_BYTES 4096
#define MAX_RESPONSE_BYTES 24576

static const char *TAG = "usage";

static usage_meters_cb_t on_meters;
static usage_attraction_cb_t on_attraction;
static usage_status_cb_t on_status;
static TaskHandle_t poll_task;

static char codex_until[16];
static char codex_week[16];
static char codex_resets[16];
static char cursor_on_demand[24];
static char cursor_team[28];
static char cursor_until[16];

static int remaining_from_used(double used_percent) {
  int left = (int)(100.0 - used_percent + 0.5);
  if (left < 0) {
    return 0;
  }
  if (left > 100) {
    return 100;
  }
  return left;
}

static void format_duration(int seconds, char *buf, size_t n) {
  if (seconds < 0) {
    seconds = 0;
  }
  int days = seconds / 86400;
  int hours = (seconds % 86400) / 3600;
  int minutes = (seconds % 3600) / 60;
  if (days > 0) {
    snprintf(buf, n, "%dd %dh", days, hours);
  } else {
    snprintf(buf, n, "%dh %dm", hours, minutes);
  }
}

/* Whole dollars only: cents make this line wrap in the 150 px center column. */
static void format_usd_whole(int cents, const char *suffix, char *buf, size_t n) {
  int dollars = (cents + (cents < 0 ? -50 : 50)) / 100;
  snprintf(buf, n, "$%d %s", dollars, suffix);
}

static void format_usd_cents(int cents, const char *suffix, char *buf, size_t n) {
  int dollars = cents / 100;
  int remainder = cents % 100;
  if (remainder < 0) {
    remainder = -remainder;
  }
  snprintf(buf, n, "$%d.%02d %s", dollars, remainder, suffix);
}

static int seconds_from_reset(cJSON *window) {
  cJSON *after = cJSON_GetObjectItemCaseSensitive(window, "reset_after_seconds");
  if (cJSON_IsNumber(after)) {
    return (int)after->valuedouble;
  }
  cJSON *at = cJSON_GetObjectItemCaseSensitive(window, "reset_at");
  if (cJSON_IsNumber(at)) {
    time_t now = time(NULL);
    if (now > 100000) {
      int left = (int)(at->valuedouble - (double)now);
      return left > 0 ? left : 0;
    }
  }
  return -1;
}

static cJSON *child(cJSON *parent, const char *camel, const char *snake) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, camel);
  if (item != NULL) {
    return item;
  }
  return cJSON_GetObjectItemCaseSensitive(parent, snake);
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
        break;
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
  buffer[received] = '\0';
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  *body = buffer;
  *body_len = received;
  return ESP_OK;
}

static bool fill_codex(cJSON *root, ui_codex_data_t *out) {
  cJSON *rate = cJSON_GetObjectItemCaseSensitive(root, "rate_limit");
  cJSON *primary = cJSON_GetObjectItemCaseSensitive(rate, "primary_window");
  cJSON *weekly = cJSON_GetObjectItemCaseSensitive(rate, "secondary_window");
  cJSON *credits = cJSON_GetObjectItemCaseSensitive(root, "rate_limit_reset_credits");
  if (!cJSON_IsObject(primary) || !cJSON_IsObject(weekly)) {
    return false;
  }
  cJSON *primary_used = cJSON_GetObjectItemCaseSensitive(primary, "used_percent");
  cJSON *weekly_used = cJSON_GetObjectItemCaseSensitive(weekly, "used_percent");
  int primary_reset = seconds_from_reset(primary);
  int weekly_reset = seconds_from_reset(weekly);
  cJSON *resets = cJSON_GetObjectItemCaseSensitive(credits, "available_count");
  if (!cJSON_IsNumber(primary_used) || !cJSON_IsNumber(weekly_used)) {
    return false;
  }
  out->primary_left_pct = remaining_from_used(primary_used->valuedouble);
  out->weekly_left_pct = remaining_from_used(weekly_used->valuedouble);
  if (primary_reset >= 0) {
    format_duration(primary_reset, codex_until, sizeof(codex_until));
  } else {
    snprintf(codex_until, sizeof(codex_until), "reset n/a");
  }
  if (weekly_reset >= 0) {
    format_duration(weekly_reset, codex_week, sizeof(codex_week));
  } else {
    snprintf(codex_week, sizeof(codex_week), "reset n/a");
  }
  snprintf(codex_resets, sizeof(codex_resets), "%d free resets",
           cJSON_IsNumber(resets) ? resets->valueint : 0);
  out->primary_until = codex_until;
  out->weekly_reset = codex_week;
  out->free_resets = codex_resets;
  return true;
}

static bool fill_cursor(cJSON *root, ui_cursor_data_t *out) {
  cJSON *individual = child(root, "individualUsage", "individual_usage");
  cJSON *plan = cJSON_GetObjectItemCaseSensitive(individual, "plan");
  cJSON *on_demand = cJSON_GetObjectItemCaseSensitive(individual, "onDemand");
  cJSON *team_usage = child(root, "teamUsage", "team_usage");
  cJSON *team = cJSON_GetObjectItemCaseSensitive(team_usage, "onDemand");
  cJSON *auto_used = cJSON_GetObjectItemCaseSensitive(plan, "autoPercentUsed");
  cJSON *api_used = cJSON_GetObjectItemCaseSensitive(plan, "apiPercentUsed");
  if (!cJSON_IsNumber(auto_used) || !cJSON_IsNumber(api_used)) {
    return false;
  }
  out->auto_left_pct = remaining_from_used(auto_used->valuedouble);
  out->named_left_pct = remaining_from_used(api_used->valuedouble);

  cJSON *used = cJSON_GetObjectItemCaseSensitive(on_demand, "used");
  format_usd_cents(cJSON_IsNumber(used) ? used->valueint : 0, "used", cursor_on_demand,
                   sizeof(cursor_on_demand));
  cJSON *remaining = cJSON_GetObjectItemCaseSensitive(team, "remaining");
  if (cJSON_IsNumber(remaining)) {
    format_usd_whole(remaining->valueint, "team left", cursor_team, sizeof(cursor_team));
  } else {
    snprintf(cursor_team, sizeof(cursor_team), "no team cap");
  }
  cJSON *cycle_end = child(root, "billingCycleEnd", "billing_cycle_end");
  bool until_set = false;
  if (cJSON_IsString(cycle_end) && cycle_end->valuestring != NULL) {
    struct tm parts = {0};
    const char *iso = cycle_end->valuestring;
    if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &parts.tm_year, &parts.tm_mon, &parts.tm_mday,
               &parts.tm_hour, &parts.tm_min, &parts.tm_sec) >= 3 ||
        sscanf(iso, "%d-%d-%d", &parts.tm_year, &parts.tm_mon, &parts.tm_mday) >= 3) {
      parts.tm_year -= 1900;
      parts.tm_mon -= 1;
      time_t now = time(NULL);
      time_t end = mktime(&parts);
      if (now > 100000 && end > 0) {
        format_duration((int)(end - now), cursor_until, sizeof(cursor_until));
        until_set = true;
      }
    }
  }
  if (!until_set) {
    snprintf(cursor_until, sizeof(cursor_until), "reset n/a");
  }
  out->on_demand_used = cursor_on_demand;
  out->team_on_demand_left = cursor_team;
  out->until_reset = cursor_until;
  return true;
}

/* Short enough for the 240 px attraction hint line. */
static void report_failure(const char *service, esp_err_t err, int status_code) {
  char text[32];
  if (err == ESP_ERR_NO_MEM) {
    snprintf(text, sizeof(text), "%s LOW MEMORY", service);
  } else if (status_code == 200) {
    snprintf(text, sizeof(text), "%s READ FAILED", service);
  } else if (status_code > 0) {
    snprintf(text, sizeof(text), "%s HTTP %d", service, status_code);
  } else {
    snprintf(text, sizeof(text), "%s UNREACHABLE", service);
  }
  ESP_LOGW(TAG, "%s fetch failed: %s (HTTP %d, %u bytes free)", service, esp_err_to_name(err),
           status_code, (unsigned)esp_get_free_heap_size());
  if (on_status != NULL) {
    on_status(text);
  }
}

/*
 * Fetch and parse one service. The response body and its JSON tree are released
 * before returning so the next TLS session starts with the heap it needs.
 */
static bool fetch_service(const char *service, const char *url, const char *headers[][2],
                          size_t header_count, bool (*fill)(cJSON *, void *), void *out) {
  char *body = NULL;
  int body_len = 0;
  int status_code = 0;
  esp_err_t err = https_get(url, headers, header_count, &body, &body_len, &status_code);
  if (err != ESP_OK) {
    report_failure(service, err, status_code);
    return false;
  }

  cJSON *json = cJSON_Parse(body);
  memset(body, 0, (size_t)body_len);
  free(body);
  bool ok = json != NULL && fill(json, out);
  cJSON_Delete(json);
  if (!ok) {
    ESP_LOGW(TAG, "%s returned unexpected JSON", service);
    if (on_status != NULL) {
      char text[32];
      snprintf(text, sizeof(text), "%s BAD JSON", service);
      on_status(text);
    }
  }
  return ok;
}

static bool fill_cursor_any(cJSON *root, void *out) {
  return fill_cursor(root, out);
}

static bool fill_codex_any(cJSON *root, void *out) {
  return fill_codex(root, out);
}

static bool refresh_usage(void) {
  credential_snapshot_t snapshot = {0};
  if (!credential_server_snapshot(&snapshot)) {
    return false;
  }

  const char *cursor_headers[][2] = {
      {"Cookie", snapshot.cursor_cookie},
      {"Origin", "https://cursor.com"},
      {"Accept", "application/json"},
      {"User-Agent", "ai-wroom-32/0.1"},
  };
  ui_cursor_data_t cursor = {0};
  bool ok = fetch_service("CURSOR", CURSOR_USAGE_URL, cursor_headers, 4, fill_cursor_any, &cursor);

  ui_codex_data_t codex = {0};
  if (ok) {
    size_t auth_len = strlen(snapshot.codex_access_token) + 8;
    char *auth = malloc(auth_len);
    if (auth == NULL) {
      credential_snapshot_free(&snapshot);
      return false;
    }
    snprintf(auth, auth_len, "Bearer %s", snapshot.codex_access_token);
    const char *codex_headers[][2] = {
        {"Authorization", auth},
        {"ChatGPT-Account-Id", snapshot.codex_account_id},
        {"Accept", "application/json"},
        {"User-Agent", "ai-wroom-32/0.1"},
    };
    ok = fetch_service("CODEX", CODEX_USAGE_URL, codex_headers, 4, fill_codex_any, &codex);
    memset(auth, 0, auth_len);
    free(auth);
  }
  credential_snapshot_free(&snapshot);
  if (!ok) {
    return false;
  }
  if (on_meters != NULL) {
    on_meters(&codex, &cursor);
  }
  return true;
}

static void on_credentials(bool present) {
  if (!present && on_attraction != NULL) {
    on_attraction();
  }
  if (poll_task != NULL) {
    xTaskNotifyGive(poll_task);
  }
}

static void poll_loop(void *arg) {
  (void)arg;
  while (true) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(POLL_PERIOD_MS));
    if (!credential_server_has_credentials()) {
      continue;
    }
    if (refresh_usage()) {
      ESP_LOGI(TAG, "Usage screens updated");
    }
  }
}

esp_err_t usage_poll_start(usage_meters_cb_t meters_cb, usage_attraction_cb_t attraction_cb,
                           usage_status_cb_t status_cb) {
  on_meters = meters_cb;
  on_attraction = attraction_cb;
  on_status = status_cb;
  credential_server_set_listener(on_credentials);
  if (xTaskCreate(poll_loop, "usage", 16384, NULL, 5, &poll_task) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
