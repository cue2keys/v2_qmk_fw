#pragma once

#include QMK_KEYBOARD_H

#define PENDANT_LED_W GP10
#define PENDANT_LED_G GP11
#define PENDANT_LED_Y GP12
#define PENDANT_LED_R GP13

// tmp mode has higher priority than base mode, and when tmp mode is deactivated, base mode is restored.
typedef enum __PendantLED_MODE_BASE { PendantLED_MODE_BASE_Off = 0, PendantLED_MODE_BASE_Layer, PendantLED_MODE_BASE_RandomOnType, PendantLED_MODE_BASE_Scanning, PendantLED_MODE_BASE_MAX } PendantLED_MODE_BASE;

void pendant_led_init(void);

void                 pendant_led_set_mode_base(PendantLED_MODE_BASE mode);
PendantLED_MODE_BASE pendant_led_get_mode_base(void);

void pendant_led_set_uint8(uint8_t value);
void pendant_led_set_on_typing(void);
void pendant_led_refresh(layer_state_t current_layer_value);
