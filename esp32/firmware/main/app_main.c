#include "displays.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network.h"
#include "ui.h"
#include "screen_config.h"
#include "usage_poll.h"

static const char *TAG = "app";

static lv_display_t *displays[SCREEN_MAX_COUNT];
static size_t screen_count;
static ui_attraction_t attraction;
static ui_face_t meters[SCREEN_MAX_COUNT];
static provider_id_t assignments[SCREEN_MAX_COUNT];
static uint32_t attraction_seed;
static uint16_t boot_pin;
static bool showing_meters;

static void show_attraction(void) {
  const char *ip = network_sta_ip();
  lvgl_port_lock(0);
  ui_attraction_destroy(&attraction);
  ui_attraction_create(displays, assignments, screen_count, ip[0] != '\0' ? ip : NULL,
                       network_pairing_pin(), attraction_seed, &attraction);
  showing_meters = false;
  network_set_attraction(&attraction);
  lvgl_port_unlock();
}

static void on_update(provider_id_t provider, const provider_data_t *data) {
  lvgl_port_lock(0);
  if (!showing_meters) {
    ui_attraction_destroy(&attraction);
    network_set_attraction(NULL);
    for (size_t i = 0; i < screen_count; ++i) {
      ui_face_create(displays[i], assignments[i], &meters[i]);
    }
    showing_meters = true;
  }
  for (size_t i = 0; i < screen_count; ++i) {
    if (assignments[i] == provider) ui_provider_apply(&meters[i], provider, data);
  }
  lvgl_port_unlock();
}

static void on_attraction(void) {
  if (showing_meters) {
    show_attraction();
  }
}


void app_main(void) {
  ESP_ERROR_CHECK(screen_config_init());
  const screen_config_t *config = screen_config_active();
  screen_count = config->count;
  uint32_t enabled = 0;
  for (size_t i = 0; i < screen_count; ++i) {
    assignments[i] = config->screens[i].provider;
    enabled |= 1U << assignments[i];
  }
  ESP_ERROR_CHECK(displays_init(config, displays));
  /* Poll each selected service once, even when several screens show it. */
  usage_poll_set_enabled(enabled);

  attraction_seed = esp_random();
  boot_pin = attraction_seed % 10000;
  lvgl_port_lock(0);
  ui_attraction_create(displays, assignments, screen_count, NULL, boot_pin, attraction_seed,
                       &attraction);
  lvgl_port_unlock();

  ESP_ERROR_CHECK(usage_poll_start(on_update, on_attraction));
  ESP_ERROR_CHECK(network_start(&attraction, boot_pin));
  ESP_LOGI(TAG, "Attraction screen, credential intake, and usage poller running");
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
