#pragma once

#include <stdint.h>
#include "kb_config.h"

#ifdef POINTING_DEVICE_ENABLE

#    include "drivers/modular_adns5050.h"
#    include "drivers/modular_pmw3610.h"

#    define TRACKBALL_SLOT_COUNT NUM_MODULAR_PMW3610
#    define SCROLL_DIVISOR_H 32.0f
#    define SCROLL_DIVISOR_V 32.0f

typedef enum {
    TRACKBALL_SENSOR_NONE = 0,
    TRACKBALL_SENSOR_PMW3610,
    TRACKBALL_SENSOR_ADNS5050,
} trackball_sensor_type_t;

uint16_t                calc_auto_mouse_timeout_by_kbconfig(uint8_t value);
uint16_t                calc_cpi_by_kbconfig(uint8_t cpi_step);
uint8_t                 trackball_get_connected_flags(uint8_t *out, uint8_t max);
uint8_t                 trackball_get_connected_count(void);
void                    trackball_reset_drag_scroll_accumulator(void);
void                    trackball_apply_sensor_config(uint8_t index);
void                    trackball_apply_timeout(uint8_t timeout_setting);
void                    trackball_wake_connected(void);
trackball_sensor_type_t trackball_get_sensor_type(uint8_t index);

void keyboard_post_init_kb_trackball(const kb_config_t *kb_cfg);
bool process_record_kb_trackball(const kb_config_t *kb_cfg, uint16_t keycode, keyrecord_t *record);

#endif
