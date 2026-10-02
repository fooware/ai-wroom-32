#pragma once
#include "esp_err.h"
#include "provider.h"
#include <stddef.h>
#include <stdint.h>

#define SCREEN_MAX_COUNT 3

typedef struct { const char *id; uint16_t width; uint16_t height; } screen_type_t;
typedef struct { unsigned type; provider_id_t provider; } screen_assignment_t;
typedef struct { size_t count; screen_assignment_t screens[SCREEN_MAX_COUNT]; } screen_config_t;
typedef struct { int cs; int reset; } screen_pins_t;

const screen_type_t *screen_type(unsigned type);
const screen_pins_t *screen_pins(size_t slot);
int screen_sclk(void);
int screen_mosi(void);
int screen_dc(void);
esp_err_t screen_config_init(void);
const screen_config_t *screen_config_active(void);
/* Validate the entire replacement before touching NVS. Device/IP and secrets are not stored. */
esp_err_t screen_config_parse(const char *json, screen_config_t *out);
esp_err_t screen_config_validate(const screen_config_t *config);
esp_err_t screen_config_save(const screen_config_t *config);
