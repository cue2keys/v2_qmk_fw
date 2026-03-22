#pragma once

#include QMK_KEYBOARD_H

#ifdef OLED_ENABLE
typedef enum _DisplayMode {
    DisplayMode_Info,
    DisplayMode_U1Walking,
    DisplayMode_SelectedKey,
    DisplayMode_InputDevice,
    DisplayMode_MAX
} DisplayMode;

bool    process_record_kb_display(uint16_t keycode, keyrecord_t *record);
uint8_t display_get_mode(void);
void    display_set_mode(DisplayMode mode);
void    display_set_keypress_target(uint8_t row, uint8_t col);
void    display_record_key_input(uint8_t row, uint8_t col);
void    display_record_encoder_input(uint8_t index);

#endif
