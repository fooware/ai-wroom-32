#include "displays.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network.h"
#include "ui.h"
#include "usage_poll.h"

static const char *TAG = "app";

static lv_display_t *left_display;
static lv_display_t *right_display;
static ui_attraction_t attraction;
static ui_demo_t meters;
static uint32_t attraction_seed;
static uint16_t boot_pin;
static bool showing_meters;

static void show_attraction(void) {
  const char *ip = network_sta_ip();
  lvgl_port_lock(0);
  ui_attraction_destroy(&attraction);
  ui_attraction_create(left_display, right_display, ip[0] != '\0' ? ip : NULL,
                       network_pairing_pin(), attraction_seed, &attraction);
  showing_meters = false;
  network_set_attraction(&attraction);
  lvgl_port_unlock();
}

static void on_meters(const ui_codex_data_t *codex, const ui_cursor_data_t *cursor) {
  lvgl_port_lock(0);
  if (!showing_meters) {
    ui_attraction_destroy(&attraction);
    network_set_attraction(NULL);
    ui_demo_create(left_display, right_display, &meters);
    showing_meters = true;
  }
  ui_codex_apply(&meters.codex, codex);
  ui_cursor_apply(&meters.cursor, cursor);
  lvgl_port_unlock();
}

static void on_attraction(void) {
  if (showing_meters) {
    show_attraction();
  }
}

static void on_status(const char *status) {
  if (showing_meters) {
    return;
  }
  lvgl_port_lock(0);
  ui_attraction_set_status(&attraction, status);
  lvgl_port_unlock();
}

void app_main(void) {
  ESP_LOGI(TAG, "Initializing dual GC9A01 + LVGL");
  ESP_ERROR_CHECK(displays_init(&left_display, &right_display));

  attraction_seed = esp_random();
  boot_pin = attraction_seed % 10000;
  lvgl_port_lock(0);
  ui_attraction_create(left_display, right_display, NULL, boot_pin, attraction_seed,
                       &attraction);
  lvgl_port_unlock();

  ESP_ERROR_CHECK(usage_poll_start(on_meters, on_attraction, on_status));
  ESP_ERROR_CHECK(network_start(&attraction, boot_pin));
  ESP_LOGI(TAG, "Attraction screen, credential intake, and usage poller running");
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
