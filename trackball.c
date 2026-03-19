#include QMK_KEYBOARD_H
#include <stdbool.h>
#include <stdint.h>

#include "trackball.h"

#ifdef POINTING_DEVICE_ENABLE

static trackball_sensor_type_t trackball_sensor_type[TRACKBALL_SLOT_COUNT] = {TRACKBALL_SENSOR_NONE};
static bool                    trackball_connected[TRACKBALL_SLOT_COUNT]   = {false};
static uint16_t                trackball_effective_cpi[TRACKBALL_SLOT_COUNT];
static bool                    set_scrolling        = false;
static float                   scroll_accumulated_h = 0.0f;
static float                   scroll_accumulated_v = 0.0f;

uint16_t calc_auto_mouse_timeout_by_kbconfig(uint8_t value) {
    return 100 * value;
}

uint16_t calc_cpi_by_kbconfig(uint8_t cpi_step) {
    uint16_t cpi = (uint16_t)((cpi_step + 1) * PMW3610_CPI_INTERVAL);
    if (cpi < PMW3610_CPI_MIN) cpi = PMW3610_CPI_MIN;
    if (cpi > PMW3610_CPI_MAX) cpi = PMW3610_CPI_MAX;
    return cpi;
}

static bool trackball_slot_valid(uint8_t index) {
    return index < TRACKBALL_SLOT_COUNT;
}

static uint16_t calc_adns5050_cpi(uint16_t requested_cpi) {
    uint16_t clamped = requested_cpi;
    if (clamped < ADNS5050_CPI_MIN) clamped = ADNS5050_CPI_MIN;
    if (clamped > ADNS5050_CPI_MAX) clamped = ADNS5050_CPI_MAX;

    uint16_t step = (uint16_t)((clamped + (ADNS5050_CPI_INTERVAL / 2)) / ADNS5050_CPI_INTERVAL);
    if (step == 0) step = 1;

    uint16_t effective_cpi = (uint16_t)(step * ADNS5050_CPI_INTERVAL);
    if (effective_cpi < ADNS5050_CPI_MIN) effective_cpi = ADNS5050_CPI_MIN;
    if (effective_cpi > ADNS5050_CPI_MAX) effective_cpi = ADNS5050_CPI_MAX;
    return effective_cpi;
}

static void trackball_probe_slot(uint8_t index) {
    trackball_sensor_type[index]   = TRACKBALL_SENSOR_NONE;
    trackball_connected[index]     = false;
    trackball_effective_cpi[index] = 0;

    modular_pmw3610_init_slot(index);
    if (modular_pmw3610_probe(index)) {
        trackball_sensor_type[index] = TRACKBALL_SENSOR_PMW3610;
        trackball_connected[index]   = true;
        return;
    }

    modular_adns5050_init_slot(index);
    if (modular_adns5050_probe(index)) {
        trackball_sensor_type[index] = TRACKBALL_SENSOR_ADNS5050;
        trackball_connected[index]   = true;
    }
}

static report_mouse_t trackball_apply_common_report(uint8_t index, report_mouse_t mouse_report) {
    const TBConfigBits *tb = kb_config_get_tb(index);
    if (tb == NULL || !set_scrolling) {
        return mouse_report;
    }

    float scroll_magnification = 0.25f * (float)(tb->drag_scroll_speed_magnification + 1);

    scroll_accumulated_h += (float)mouse_report.x / SCROLL_DIVISOR_H;
    scroll_accumulated_v += (float)mouse_report.y / SCROLL_DIVISOR_V;

    mouse_report.h = (int8_t)(scroll_accumulated_h * scroll_magnification);
    mouse_report.v = (int8_t)(scroll_accumulated_v * scroll_magnification);

    if (tb->invert_drag_scroll_y) {
        mouse_report.h *= -1;
    }
    if (tb->invert_drag_scroll_x) {
        mouse_report.v *= -1;
    }

    scroll_accumulated_h -= (float)(int8_t)scroll_accumulated_h;
    scroll_accumulated_v -= (float)(int8_t)scroll_accumulated_v;
    mouse_report.x = 0;
    mouse_report.y = 0;
    return mouse_report;
}

uint8_t trackball_get_connected_flags(uint8_t *out, uint8_t max) {
    if (out == NULL || max == 0) {
        return 0;
    }

    uint8_t count = TRACKBALL_SLOT_COUNT;
    if (count > max) {
        count = max;
    }

    for (uint8_t i = 0; i < count; i++) {
        out[i] = trackball_connected[i] ? 1 : 0;
    }
    for (uint8_t i = count; i < max; i++) {
        out[i] = 0;
    }
    return count;
}

uint8_t trackball_get_connected_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < TRACKBALL_SLOT_COUNT; i++) {
        count += trackball_connected[i] ? 1 : 0;
    }
    return count;
}

void trackball_reset_drag_scroll_accumulator(void) {
    scroll_accumulated_h = 0.0f;
    scroll_accumulated_v = 0.0f;
}

void trackball_apply_sensor_config(uint8_t index) {
    const TBConfigBits *tb = kb_config_get_tb(index);
    if (!trackball_slot_valid(index) || tb == NULL) {
        return;
    }

    uint16_t requested_cpi = calc_cpi_by_kbconfig(tb->cpi_step);
    switch (trackball_sensor_type[index]) {
        case TRACKBALL_SENSOR_PMW3610:
            modular_pmw3610_set_angle(index, tb->angle);
            modular_pmw3610_set_cpi(index, requested_cpi);
            trackball_effective_cpi[index] = requested_cpi;
            break;
        case TRACKBALL_SENSOR_ADNS5050: {
            uint16_t effective_cpi = calc_adns5050_cpi(requested_cpi);
            modular_adns5050_set_angle(index, tb->angle);
            modular_adns5050_set_cpi(index, effective_cpi);
            trackball_effective_cpi[index] = effective_cpi;
            break;
        }
        case TRACKBALL_SENSOR_NONE:
        default:
            trackball_effective_cpi[index] = 0;
            break;
    }
}

void trackball_apply_timeout(uint8_t timeout_setting) {
    uint32_t timeout_ms = (uint32_t)timeout_setting * 5u * 60u * 1000u;
    modular_pmw3610_set_led_off_length(timeout_ms);
    modular_adns5050_set_led_off_length(timeout_ms);
}

void trackball_wake_connected(void) {
    for (uint8_t i = 0; i < TRACKBALL_SLOT_COUNT; i++) {
        if (!trackball_connected[i]) {
            continue;
        }

        switch (trackball_sensor_type[i]) {
            case TRACKBALL_SENSOR_PMW3610:
                modular_pmw3610_wake_up(i, true);
                break;
            case TRACKBALL_SENSOR_ADNS5050:
                modular_adns5050_wake_up(i, true);
                break;
            case TRACKBALL_SENSOR_NONE:
            default:
                break;
        }
    }
}

trackball_sensor_type_t trackball_get_sensor_type(uint8_t index) {
    if (!trackball_slot_valid(index)) {
        return TRACKBALL_SENSOR_NONE;
    }
    return trackball_sensor_type[index];
}

bool pointing_device_driver_init(void) {
    for (uint8_t i = 0; i < TRACKBALL_SLOT_COUNT; i++) {
        trackball_probe_slot(i);
    }
    set_auto_mouse_layer(AUTO_MOUSE_DEFAULT_LAYER); // default
    set_auto_mouse_enable(true);
    return true;
}

void keyboard_post_init_kb_trackball(const kb_config_t *kb_cfg) {
    for (uint8_t i = 0; i < TRACKBALL_SLOT_COUNT; i++) {
        trackball_apply_sensor_config(i);
    }
    set_auto_mouse_timeout(calc_auto_mouse_timeout_by_kbconfig(kb_cfg->v.general.mouse_layer_off_delay_ms));
    trackball_apply_timeout(kb_cfg->v.general.trackball_timeout);
}

report_mouse_t pointing_device_driver_get_report(report_mouse_t mouse_report) {
    for (uint8_t i = 0; i < TRACKBALL_SLOT_COUNT; i++) {
        if (!trackball_connected[i]) {
            continue;
        }

        report_mouse_t slot_report = {};
        switch (trackball_sensor_type[i]) {
            case TRACKBALL_SENSOR_PMW3610:
                slot_report = modular_pmw3610_get_report(i, slot_report);
                break;
            case TRACKBALL_SENSOR_ADNS5050:
                slot_report = modular_adns5050_get_report(i, slot_report);
                break;
            case TRACKBALL_SENSOR_NONE:
            default:
                continue;
        }

        slot_report = trackball_apply_common_report(i, slot_report);
        mouse_report.x += slot_report.x;
        mouse_report.y += slot_report.y;
        mouse_report.h += slot_report.h;
        mouse_report.v += slot_report.v;
    }

    modular_pmw3610_check_timeout();
    modular_adns5050_check_timeout();
    return mouse_report;
}

// uint16_t pointing_device_driver_get_cpi(void) {
//     return modular_pmw3610_get_all_cpi();
// }
// void pointing_device_driver_set_cpi(uint16_t cpi) {
//     modular_pmw3610_set_all_cpi(cpi);
// }

bool is_mouse_record_kb(uint16_t keycode, keyrecord_t *record) {
    const kb_config_t *kb = kb_config_get();
    if (layer_state_is(get_auto_mouse_layer()) && kb->v.general.all_keys_are_mouse_keys) {
        return true;
    }

    return is_mouse_record_user(keycode, record);
}

bool process_record_kb_trackball(const kb_config_t *kb_cfg, uint16_t keycode, keyrecord_t *record) {
    (void)kb_cfg;
    switch (keycode) {
        case DRAG_SCROLL:
            set_scrolling = record->event.pressed;
            return false;
    }
    return true;
}

#endif
