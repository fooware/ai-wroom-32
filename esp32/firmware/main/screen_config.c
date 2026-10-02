#include "screen_config.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const screen_type_t types[] = {{"gc9a01_240", 240, 240}, {"gc9b72_360", 360, 360}};
static const screen_pins_t pins[SCREEN_MAX_COUNT] = {
  {CONFIG_METER_SCREEN_1_CS, CONFIG_METER_SCREEN_1_RST},
  {CONFIG_METER_SCREEN_2_CS, CONFIG_METER_SCREEN_2_RST},
  {CONFIG_METER_SCREEN_3_CS, CONFIG_METER_SCREEN_3_RST},
};
static screen_config_t active;

static bool object_has_only_unique_keys(const cJSON *object, const char *const *keys, size_t key_count) {
  if (!cJSON_IsObject(object)) return false;
  for (const cJSON *item = object->child; item; item = item->next) {
    bool known = false;
    for (size_t i = 0; i < key_count; ++i) {
      if (strcmp(item->string, keys[i]) == 0) { known = true; break; }
    }
    if (!known) return false;
    for (const cJSON *prior = object->child; prior != item; prior = prior->next)
      if (strcmp(item->string, prior->string) == 0) return false;
  }
  return true;
}

const screen_type_t *screen_type(unsigned type) {
  return type < sizeof(types) / sizeof(types[0]) ? &types[type] : NULL;
}
const screen_pins_t *screen_pins(size_t slot) { return slot < SCREEN_MAX_COUNT ? &pins[slot] : NULL; }
int screen_sclk(void) { return CONFIG_METER_SCLK; }
int screen_mosi(void) { return CONFIG_METER_MOSI; }
int screen_dc(void) { return CONFIG_METER_DC; }
const screen_config_t *screen_config_active(void) { return &active; }

esp_err_t screen_config_validate(const screen_config_t *config) {
  if (!config || config->count < 1 || config->count > SCREEN_MAX_COUNT) return ESP_ERR_INVALID_ARG;
  int shared[] = {screen_sclk(), screen_mosi(), screen_dc()};
  for (size_t i = 0; i < 3; ++i) {
    if (!GPIO_IS_VALID_OUTPUT_GPIO(shared[i])) return ESP_ERR_INVALID_ARG;
#if CONFIG_IDF_TARGET_ESP32
    if (shared[i] >= 6 && shared[i] <= 11) return ESP_ERR_INVALID_ARG;
#endif
    for (size_t j = 0; j < i; ++j) if (shared[j] == shared[i]) return ESP_ERR_INVALID_ARG;
  }
  for (size_t i = 0; i < config->count; ++i) {
    if (!screen_type(config->screens[i].type) || !provider_info(config->screens[i].provider) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(pins[i].cs) ||
        (pins[i].reset != -1 && !GPIO_IS_VALID_OUTPUT_GPIO(pins[i].reset))) return ESP_ERR_INVALID_ARG;
#if CONFIG_IDF_TARGET_ESP32
    if ((pins[i].cs >= 6 && pins[i].cs <= 11) ||
        (pins[i].reset >= 6 && pins[i].reset <= 11)) return ESP_ERR_INVALID_ARG;
#endif
    for (size_t j = 0; j < 3; ++j)
      if (pins[i].cs == shared[j] || pins[i].reset == shared[j]) return ESP_ERR_INVALID_ARG;
    for (size_t j = 0; j < config->count; ++j) {
      if (i != j && pins[i].cs == pins[j].cs) return ESP_ERR_INVALID_ARG;
      if (pins[i].cs == pins[j].reset) return ESP_ERR_INVALID_ARG;
    }
  }
  return ESP_OK;
}

esp_err_t screen_config_parse(const char *json, screen_config_t *out) {
  if (!json || !out) return ESP_ERR_INVALID_ARG;
  cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
  cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
  cJSON *screens = cJSON_GetObjectItemCaseSensitive(root, "screens");
  screen_config_t candidate = {0};
  static const char *const root_keys[] = {"version", "screens"};
  static const char *const entry_keys[] = {"type", "provider"};
  bool valid = object_has_only_unique_keys(root, root_keys, 2) &&
               cJSON_IsNumber(version) && version->valuedouble == 1 && cJSON_IsArray(screens);
  int count = cJSON_GetArraySize(screens);
  valid = valid && count >= 1 && count <= SCREEN_MAX_COUNT;
  candidate.count = valid ? (size_t)count : 0;
  for (size_t i = 0; valid && i < candidate.count; ++i) {
    cJSON *entry = cJSON_GetArrayItem(screens, (int)i);
    cJSON *type = cJSON_GetObjectItemCaseSensitive(entry, "type");
    cJSON *provider = cJSON_GetObjectItemCaseSensitive(entry, "provider");
    valid = object_has_only_unique_keys(entry, entry_keys, 2) &&
            cJSON_IsString(type) && cJSON_IsString(provider);
    if (!valid) break;
    valid = provider_from_name(provider->valuestring, &candidate.screens[i].provider);
    bool found = false;
    for (unsigned t = 0; screen_type(t); ++t) {
      if (strcmp(type->valuestring, screen_type(t)->id) == 0) { candidate.screens[i].type = t; found = true; break; }
    }
    valid = valid && found;
  }
  cJSON_Delete(root);
  if (!valid || screen_config_validate(&candidate) != ESP_OK) return ESP_ERR_INVALID_ARG;
  *out = candidate;
  return ESP_OK;
}

esp_err_t screen_config_save(const screen_config_t *config) {
  esp_err_t err = screen_config_validate(config);
  if (err != ESP_OK) return err;
  cJSON *root = cJSON_CreateObject();
  if (!root) return ESP_ERR_NO_MEM;
  bool ok = cJSON_AddNumberToObject(root, "version", 1) != NULL;
  cJSON *screens = cJSON_AddArrayToObject(root, "screens");
  ok = ok && screens;
  for (size_t i = 0; ok && i < config->count; ++i) {
    cJSON *entry = cJSON_CreateObject();
    if (!entry) { ok = false; break; }
    if (!cJSON_AddItemToArray(screens, entry)) { cJSON_Delete(entry); ok = false; break; }
    ok = cJSON_AddStringToObject(entry, "type", screen_type(config->screens[i].type)->id) &&
         cJSON_AddStringToObject(entry, "provider", provider_info(config->screens[i].provider)->id);
  }
  char *json = ok ? cJSON_PrintUnformatted(root) : NULL;
  cJSON_Delete(root);
  if (!json) return ESP_ERR_NO_MEM;
  nvs_handle_t handle;
  err = nvs_open("screens", NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    err = nvs_set_str(handle, "config", json);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
  }
  free(json);
  return err;
}

esp_err_t screen_config_init(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    err = nvs_flash_erase();
    if (err == ESP_OK) err = nvs_flash_init();
  }
  if (err != ESP_OK) return err;
  active = (screen_config_t){.count = 2, .screens = {{0, PROVIDER_CODEX}, {0, PROVIDER_CURSOR}}};
  if (screen_config_validate(&active) != ESP_OK) {
    active.count = 1;
    if (screen_config_validate(&active) != ESP_OK) return ESP_ERR_INVALID_ARG;
  }
  nvs_handle_t handle;
  err = nvs_open("screens", NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (err != ESP_OK) return err;
  size_t length = 0;
  err = nvs_get_str(handle, "config", NULL, &length);
  if (err == ESP_OK && length <= 4096) {
    char *json = malloc(length);
    if (!json) { nvs_close(handle); return ESP_ERR_NO_MEM; }
    err = nvs_get_str(handle, "config", json, &length);
    screen_config_t loaded;
    if (err == ESP_OK && screen_config_parse(json, &loaded) == ESP_OK) active = loaded;
    else ESP_LOGW("screens", "Stored screen configuration invalid for this firmware; using defaults");
    free(json);
  }
  nvs_close(handle);
  /* Invalid/obsolete stored config falls back to the wired defaults. */
  return ESP_OK;
}
