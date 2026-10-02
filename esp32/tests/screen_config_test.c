#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "screen_config.h"
#include "nvs.h"

static char *saved;
static int rejected_gpio = -999;
static esp_err_t flash_result = ESP_OK;
static int erase_calls;

int fake_gpio_valid(int gpio) { return gpio >= 0 && gpio <= 39 && gpio != rejected_gpio; }
esp_err_t nvs_flash_init(void) { esp_err_t result = flash_result; flash_result = ESP_OK; return result; }
esp_err_t nvs_flash_erase(void) { ++erase_calls; free(saved); saved = NULL; return ESP_OK; }
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out) {
  (void)name; (void)mode; *out = 1; return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value) {
  (void)handle; (void)key; free(saved); saved = strdup(value); return saved ? ESP_OK : ESP_ERR_NO_MEM;
}
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out, size_t *length) {
  (void)handle; (void)key;
  if (!saved) return ESP_ERR_NVS_NOT_FOUND;
  size_t need = strlen(saved) + 1;
  if (!out) { *length = need; return ESP_OK; }
  if (*length < need) return ESP_FAIL;
  memcpy(out, saved, need); *length = need; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) { (void)handle; return ESP_OK; }
void nvs_close(nvs_handle_t handle) { (void)handle; }

static screen_config_t config(size_t count) {
  screen_config_t value = {.count = count};
  for (size_t i = 0; i < count; ++i) {
    value.screens[i].type = 0;
    value.screens[i].provider = (provider_id_t)i;
  }
  return value;
}

static void test_parse_counts(void) {
  screen_config_t out;
  assert(screen_config_parse("{\"version\":1,\"screens\":[{\"type\":\"gc9a01_240\",\"provider\":\"codex\"}]}", &out) == ESP_OK && out.count == 1);
  assert(screen_config_parse("{\"version\":1,\"screens\":[{\"type\":\"gc9a01_240\",\"provider\":\"codex\"},{\"type\":\"gc9a01_240\",\"provider\":\"cursor\"}]}", &out) == ESP_OK && out.count == 2);
  assert(screen_config_parse("{\"version\":1,\"screens\":[{\"type\":\"gc9a01_240\",\"provider\":\"codex\"},{\"type\":\"gc9a01_240\",\"provider\":\"cursor\"},{\"type\":\"gc9a01_240\",\"provider\":\"claude\"}]}", &out) == ESP_OK && out.count == 3);
}
static void test_invalid_input_and_wiring(void) {
  screen_config_t out;
  assert(screen_config_parse("{\"version\":1,\"screens\":[]}", &out) == ESP_ERR_INVALID_ARG);
  assert(screen_config_parse("{\"version\":true,\"screens\":[]}", &out) == ESP_ERR_INVALID_ARG);
  assert(screen_config_parse("{\"version\":1,\"extra\":0,\"screens\":[{\"type\":\"gc9a01_240\",\"provider\":\"codex\"}]}", &out) == ESP_ERR_INVALID_ARG);
  assert(screen_config_parse("{\"version\":1,\"screens\":[{\"type\":\"gc9a01_240\",\"provider\":\"codex\",\"extra\":0}]}", &out) == ESP_ERR_INVALID_ARG);
  assert(screen_config_parse("{\"version\":1,\"version\":1,\"screens\":[{\"type\":\"gc9a01_240\",\"provider\":\"codex\"}]}", &out) == ESP_ERR_INVALID_ARG);
  assert(screen_config_parse("{\"version\":1,\"screens\":[{\"type\":\"gc9a01_240\",\"provider\":\"codex\"},{\"type\":\"gc9a01_240\",\"provider\":\"cursor\"},{\"type\":\"gc9a01_240\",\"provider\":\"claude\"},{\"type\":\"gc9a01_240\",\"provider\":\"codex\"}]}", &out) == ESP_ERR_INVALID_ARG);
  assert(screen_config_parse("{\"version\":1,\"screens\":[{\"type\":\"bad\",\"provider\":\"codex\"}]}", &out) == ESP_ERR_INVALID_ARG);
  assert(screen_config_parse("{\"version\":1,\"screens\":[{\"type\":\"gc9a01_240\",\"provider\":\"bad\"}]}", &out) == ESP_ERR_INVALID_ARG);
  screen_config_t value = config(1); value.screens[0].type = 8;
  assert(screen_config_validate(&value) == ESP_ERR_INVALID_ARG);
  value = config(1); value.screens[0].provider = PROVIDER_COUNT;
  assert(screen_config_validate(&value) == ESP_ERR_INVALID_ARG);
  value = config(3); rejected_gpio = 21;
  assert(screen_config_validate(&value) == ESP_ERR_INVALID_ARG);
  rejected_gpio = -999;
  out = config(1);
  screen_config_t before = out;
  assert(screen_config_parse("{\"version\":1,\"screens\":[]}", &out) == ESP_ERR_INVALID_ARG);
  assert(memcmp(&out, &before, sizeof(out)) == 0);
}
static void test_save_and_reboot(void) {
  screen_config_t value = config(3);
  assert(screen_config_save(&value) == ESP_OK);
  assert(screen_config_init() == ESP_OK);
  const screen_config_t *active = screen_config_active();
  assert(active->count == 3 && active->screens[2].provider == PROVIDER_CLAUDE);
}
static void test_nvs_recovery(void) {
  flash_result = ESP_ERR_NVS_NO_FREE_PAGES;
  erase_calls = 0;
  assert(screen_config_init() == ESP_OK);
  assert(erase_calls == 1);
  flash_result = ESP_ERR_NVS_NEW_VERSION_FOUND;
  assert(screen_config_init() == ESP_OK);
  assert(erase_calls == 2);
}
int main(void) { test_parse_counts(); test_invalid_input_and_wiring(); test_save_and_reboot(); test_nvs_recovery(); free(saved); return 0; }
