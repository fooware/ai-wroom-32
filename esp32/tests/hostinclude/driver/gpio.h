#pragma once
#include "esp_err.h"
int fake_gpio_valid(int gpio);
#define GPIO_IS_VALID_OUTPUT_GPIO(gpio) fake_gpio_valid(gpio)
typedef struct { int mode; unsigned long long pin_bit_mask; } gpio_config_t;
#define GPIO_MODE_OUTPUT 1
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(int gpio, int level);
esp_err_t gpio_reset_pin(int gpio);
