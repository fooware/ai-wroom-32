#pragma once
#define CONFIG_METER_SCLK 5
#define CONFIG_METER_MOSI 18
#define CONFIG_METER_DC 19
#define CONFIG_METER_SCREEN_1_CS 22
#define CONFIG_METER_SCREEN_1_RST 23
#define CONFIG_METER_SCREEN_2_CS 15
#define CONFIG_METER_SCREEN_2_RST 4
/* Deliberately wired in host tests so all 1..3 configurations are testable. */
#define CONFIG_METER_SCREEN_3_CS 21
#define CONFIG_METER_SCREEN_3_RST 2
#define CONFIG_IDF_TARGET_ESP32 0
