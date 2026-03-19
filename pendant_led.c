#include QMK_KEYBOARD_H

#include "pendant_led.h"

PendantLED_MODE_BASE base_mode = PendantLED_MODE_BASE_Scanning;

static uint8_t  current_random_value = 0;
static uint8_t  scanning_led_index   = 0;
static uint32_t scanning_last_time   = 0;

static void pendant_led_write(uint8_t value) {
    gpio_write_pin(PENDANT_LED_W, (value >> 0) & 0x01);
    gpio_write_pin(PENDANT_LED_G, (value >> 1) & 0x01);
    gpio_write_pin(PENDANT_LED_Y, (value >> 2) & 0x01);
    gpio_write_pin(PENDANT_LED_R, (value >> 3) & 0x01);
}

void pendant_led_init(void) {
    gpio_set_pin_output(PENDANT_LED_W);
    gpio_set_pin_output(PENDANT_LED_G);
    gpio_set_pin_output(PENDANT_LED_Y);
    gpio_set_pin_output(PENDANT_LED_R);
}

void pendant_led_set_mode_base(PendantLED_MODE_BASE mode) {
    base_mode = mode;
}

PendantLED_MODE_BASE pendant_led_get_mode_base(void) {
    return base_mode;
}

void pendant_led_set_on_typing(void) {
    current_random_value = rand() & 0x0F;
}

void pendant_led_refresh(layer_state_t current_layer_value) {
    if (base_mode != PendantLED_MODE_BASE_Scanning) {
        scanning_led_index = 0;
        scanning_last_time = 0;
    }

    switch (base_mode) {
        case PendantLED_MODE_BASE_Off:
            pendant_led_write(0x00);
            break;

        case PendantLED_MODE_BASE_Layer:
            pendant_led_write(current_layer_value);
            break;

        case PendantLED_MODE_BASE_RandomOnType:
            pendant_led_write(current_random_value);
            break;

        case PendantLED_MODE_BASE_Scanning: {
            uint32_t current_time = timer_read32();
            if (current_time - scanning_last_time >= 100) {
                scanning_last_time = current_time;

                // 全てのLEDを消す
                pendant_led_write(0x00);

                // 現在のLEDを点灯
                switch (scanning_led_index) {
                    case 0:
                        gpio_write_pin(PENDANT_LED_W, 1);
                        break;
                    case 1:
                        gpio_write_pin(PENDANT_LED_G, 1);
                        break;
                    case 2:
                        gpio_write_pin(PENDANT_LED_Y, 1);
                        break;
                    case 3:
                        gpio_write_pin(PENDANT_LED_R, 1);
                        break;
                }

                // 次のLEDへ
                scanning_led_index = (scanning_led_index + 1) % 4;
            }
            break;
        }

        case PendantLED_MODE_BASE_MAX:
            // Invalid mode
            pendant_led_write(0x0F);
            break;
    }
}
