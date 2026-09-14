#include "network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "credential_server.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_softap.h"
#include "protocomm.h"

#define MAX_PIN_FAILURES 3
#define CONNECT_TIMEOUT_US (30 * 1000 * 1000)

static const char *TAG = "network";

typedef struct {
  ui_attraction_t *ui;
  char service_name[20];
  char provisioning_pin[5];
  uint16_t pairing_pin;
  uint8_t pin_failures;
  bool connected;
  bool provisioning;
  bool locked;
  bool wifi_handler_registered;
  bool joined_once;
  bool sntp_started;
  char sta_ip[16];
  esp_timer_handle_t connect_timer;
} network_state_t;

static network_state_t state;

static esp_err_t start_provisioning(uint16_t pin);
static void enter_softap_fallback(void *arg);

static void set_connection_text(const char *text, uint16_t pin) {
  if (state.ui == NULL) {
    return;
  }
  lvgl_port_lock(0);
  ui_attraction_set_connection(state.ui, text, pin);
  lvgl_port_unlock();
}

static void stop_connect_timer(void) {
  if (state.connect_timer != NULL) {
    esp_timer_stop(state.connect_timer);
  }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  (void)base;
  (void)data;

  if (id == WIFI_EVENT_STA_START && !state.provisioning && !state.locked) {
    esp_wifi_connect();
  } else if (id == WIFI_EVENT_STA_DISCONNECTED && !state.locked && !state.provisioning) {
    state.connected = false;
    set_connection_text("Reconnecting...", state.pairing_pin);
    esp_wifi_connect();
  }
}

static esp_err_t register_wifi_handler(void) {
  if (state.wifi_handler_registered) {
    return ESP_OK;
  }
  esp_err_t err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
  if (err == ESP_OK) {
    state.wifi_handler_registered = true;
  }
  return err;
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  (void)base;
  if (id != IP_EVENT_STA_GOT_IP) {
    return;
  }

  const ip_event_got_ip_t *event = data;
  snprintf(state.sta_ip, sizeof(state.sta_ip), IPSTR, IP2STR(&event->ip_info.ip));
  state.connected = true;
  state.joined_once = true;
  stop_connect_timer();
  if (!state.sntp_started) {
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&sntp_config) == ESP_OK) {
      state.sntp_started = true;
    } else {
      ESP_LOGW(TAG, "SNTP init failed");
    }
  }
  set_connection_text(state.sta_ip, state.pairing_pin);
  esp_err_t server_err =
      credential_server_start(state.ui, state.sta_ip, state.pairing_pin);
  if (server_err != ESP_OK) {
    ESP_LOGE(TAG, "Credential server failed: %s",
             esp_err_to_name(server_err));
  }
  ESP_LOGI(TAG, "Station connected at %s", state.sta_ip);
}

static void connect_timeout(void *arg) {
  (void)arg;
  if (state.connected || state.provisioning || state.locked || state.joined_once) {
    return;
  }
  /* Provisioning init is too heavy for the timer task. */
  if (xTaskCreate(enter_softap_fallback, "wifi_fb", 4096, NULL, 5, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Could not start SoftAP fallback task");
  }
}

static void provisioning_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  (void)data;

  if (base == PROTOCOMM_SECURITY_SESSION_EVENT &&
      id == PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH) {
    state.pin_failures++;
    ESP_LOGW(TAG, "Incorrect provisioning PIN (%u/%u)", state.pin_failures, MAX_PIN_FAILURES);
    if (state.pin_failures >= MAX_PIN_FAILURES) {
      state.locked = true;
      state.provisioning = false;
      set_connection_text("LOCKED - reboot", 0);
      network_prov_mgr_stop_provisioning();
    }
    return;
  }

  if (base != NETWORK_PROV_EVENT) {
    return;
  }

  switch (id) {
    case NETWORK_PROV_START:
      ESP_LOGI(TAG, "SoftAP provisioning started as %s", state.service_name);
      break;
    case NETWORK_PROV_WIFI_CRED_RECV:
      /* Never log the SSID or password. */
      ESP_LOGI(TAG, "Wi-Fi credentials received");
      set_connection_text("Joining Wi-Fi...", (uint16_t)strtoul(state.provisioning_pin, NULL, 10));
      break;
    case NETWORK_PROV_WIFI_CRED_FAIL:
      ESP_LOGW(TAG, "Provisioned Wi-Fi credentials did not connect");
      set_connection_text("Wi-Fi failed", (uint16_t)strtoul(state.provisioning_pin, NULL, 10));
      break;
    case NETWORK_PROV_WIFI_CRED_SUCCESS:
      ESP_LOGI(TAG, "Wi-Fi provisioning succeeded");
      break;
    case NETWORK_PROV_END:
      state.provisioning = false;
      network_prov_mgr_deinit();
      if (!state.locked) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(register_wifi_handler());
      }
      break;
    default:
      break;
  }
}

static network_prov_mgr_config_t prov_mgr_config(void) {
  return (network_prov_mgr_config_t){
      .scheme = network_prov_scheme_softap,
      .scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
      .app_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
      .network_prov_wifi_conn_cfg = {.wifi_conn_attempts = 5},
  };
}

static void enter_softap_fallback(void *arg) {
  (void)arg;
  if (state.connected || state.provisioning || state.locked || state.joined_once) {
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGW(TAG, "No IP after 30 s; starting SoftAP provisioning");
  state.provisioning = true;
  stop_connect_timer();
  esp_wifi_disconnect();
  ESP_ERROR_CHECK_WITHOUT_ABORT(network_prov_mgr_init(prov_mgr_config()));
  ESP_ERROR_CHECK_WITHOUT_ABORT(network_prov_mgr_reset_wifi_provisioning());
  if (start_provisioning(state.pairing_pin) != ESP_OK) {
    ESP_LOGE(TAG, "SoftAP fallback failed");
    state.provisioning = false;
  }
  vTaskDelete(NULL);
}

static esp_err_t init_nvs(void) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
    err = nvs_flash_init();
  }
  return err;
}

static esp_err_t start_station(void) {
  ESP_RETURN_ON_ERROR(register_wifi_handler(), TAG, "register Wi-Fi handler");
  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set station mode");
  set_connection_text("Connecting...", state.pairing_pin);

  const esp_timer_create_args_t timer_args = {
      .callback = connect_timeout,
      .name = "wifi_wait",
  };
  ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &state.connect_timer), TAG, "create connect timer");
  ESP_RETURN_ON_ERROR(esp_timer_start_once(state.connect_timer, CONNECT_TIMEOUT_US), TAG,
                      "start connect timer");
  return esp_wifi_start();
}

static esp_err_t start_provisioning(uint16_t pin) {
  uint8_t mac[6];
  ESP_RETURN_ON_ERROR(esp_wifi_get_mac(WIFI_IF_STA, mac), TAG, "read station MAC");
  snprintf(state.service_name, sizeof(state.service_name), "AIOM-%02X%02X%02X", mac[3], mac[4], mac[5]);
  snprintf(state.provisioning_pin, sizeof(state.provisioning_pin), "%04u", (unsigned)(pin % 10000));

  state.provisioning = true;
  set_connection_text(state.service_name, pin);
  /* Security 1: PIN is proof-of-possession. SoftAP Prov app speaks this. */
  esp_err_t err = network_prov_mgr_start_provisioning(
      NETWORK_PROV_SECURITY_1, state.provisioning_pin, state.service_name, NULL);
  if (err != ESP_OK) {
    state.provisioning = false;
  }
  return err;
}

esp_err_t network_start(ui_attraction_t *ui, uint16_t provisioning_pin) {
  memset(&state, 0, sizeof(state));
  state.ui = ui;
  state.pairing_pin = provisioning_pin % 10000;

  ESP_RETURN_ON_ERROR(init_nvs(), TAG, "initialize NVS");
  ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "initialize network stack");
  ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "create event loop");
  ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_sta() != NULL, ESP_FAIL, TAG, "create station netif");
  ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_ap() != NULL, ESP_FAIL, TAG, "create SoftAP netif");

  const wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_config), TAG, "initialize Wi-Fi");

  ESP_RETURN_ON_ERROR(
      esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL),
      TAG, "register IP handler");
  ESP_RETURN_ON_ERROR(
      esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, provisioning_event_handler, NULL),
      TAG, "register provisioning handler");
  ESP_RETURN_ON_ERROR(
      esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID,
                                 provisioning_event_handler, NULL),
      TAG, "register security handler");

  ESP_RETURN_ON_ERROR(network_prov_mgr_init(prov_mgr_config()), TAG, "initialize provisioning manager");

  bool provisioned = false;
  ESP_RETURN_ON_ERROR(network_prov_mgr_is_wifi_provisioned(&provisioned), TAG, "read provisioning state");
  if (provisioned) {
    ESP_RETURN_ON_ERROR(network_prov_mgr_deinit(), TAG, "release provisioning manager");
    return start_station();
  }
  return start_provisioning(provisioning_pin);
}

uint16_t network_pairing_pin(void) {
  return state.pairing_pin;
}

bool network_is_connected(void) {
  return state.connected;
}

const char *network_sta_ip(void) {
  return state.sta_ip;
}

void network_set_attraction(ui_attraction_t *ui) {
  state.ui = ui;
}
