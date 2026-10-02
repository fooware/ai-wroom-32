#pragma once
int fake_gpio_valid(int gpio);
#define GPIO_IS_VALID_OUTPUT_GPIO(gpio) fake_gpio_valid(gpio)
