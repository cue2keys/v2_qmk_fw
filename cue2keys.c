#include QMK_KEYBOARD_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "matrix.h"
#include "atomic_util.h"
#include "i2clib.h"
#include "device_scanner.h"
#include "i2c_master.h"
#include "kb_config.h"
#include "./drivers/encoder_dynamic_res.h"
#include "raw_hid.h"
#include "trackball.h"
#include "pendant_reader.h"
#include "module_reader.h"
#include "display.h"
#include "pendant_led.h"
#include "encoder.h"
#include "magkey_utils.h"

extern DeviceList deviceList[MAX_MCP_NUM];
extern uint16_t   nDevices;
extern uint8_t    encoder_data[NUM_ENCODERS];

static pin_t direct_pins[MATRIX_ROWS][MATRIX_COLS] = DIRECT_PINS;

static uint8_t magkey_current_data_bytes = MAGKEY_DATA_BYTES_MAX;

typedef struct {
    uint8_t data_bytes;
    uint8_t bits_per_value;
    uint8_t value_shift;
} magkey_runtime_config_t;

static bool handle_i2c_read_result(uint16_t index, const Keys_Data *data) {
    if (deviceList[index].consecutive_errors < UINT8_MAX) {
        deviceList[index].consecutive_errors++;
    }

    const kb_config_t *kb = kb_config_get();
    if (kb->v.general.rescan_i2c_on_read_error && deviceList[index].consecutive_errors >= I2C_READ_ERROR_RESCAN_THRESHOLD) {
        dprintf("[I2C] consecutive read errors (%u) on ch %u addr 0x%02X, rescanning,,,\n", deviceList[index].consecutive_errors, deviceList[index].ch, deviceList[index].address);
        do_scan();
        return true;
    }

    return false;
}

static magkey_runtime_config_t load_magkey_runtime_config(const kb_config_t *kb) {
    magkey_runtime_config_t config = {
        .data_bytes = magkey_data_bytes_from_config(kb->v.general.magkey_data_bytes),
    };

    if (config.data_bytes != magkey_current_data_bytes) {
        magkey_current_data_bytes = config.data_bytes;
        magkey_reset_cached_values();
    }

    config.bits_per_value = magkey_bits_per_value(config.data_bytes);
    config.value_shift    = magkey_value_shift(config.bits_per_value);

    return config;
}

static bool row_has_system_pins(uint8_t current_row) {
    return current_row >= SYSTEM_KEY_START_IDX / MATRIX_COLS && current_row < (SYSTEM_KEY_START_IDX + SYSTEM_KEY_NUM) / MATRIX_COLS;
}

static matrix_row_t read_system_row_data(uint8_t current_row) {
    matrix_row_t row_data = 0;
    if (!row_has_system_pins(current_row)) {
        return row_data;
    }

    for (uint16_t i = 0; i < SYSTEM_KEY_NUM; i++) {
        uint16_t key_idx = SYSTEM_KEY_START_IDX + i;
        pin_t    dpin    = direct_pins[key_idx / MATRIX_COLS][key_idx % MATRIX_COLS];
        if (dpin != NO_PIN) {
            uint8_t pin_state = gpio_read_pin(dpin);
            row_data |= (matrix_row_t)(0x1 ^ pin_state) << i;
        }
    }

    return row_data;
}

static bool device_has_row_data(const DeviceList *device) {
    if (device->ch == 0) {
        return false;
    }

    switch (device->type) {
        case Pendant_v2_ModuleType_DISPLAY:
        case Pendant_v2_ModuleType_I2C_MUX:
        case Pendant_v2_ModuleType_UNKNOWN:
            return false;
        default:
            return true;
    }
}

static bool device_targets_row(const DeviceList *device, uint8_t current_row) {
    uint16_t row_start = (uint16_t)MATRIX_COLS * current_row;
    uint16_t row_end   = row_start + MATRIX_COLS;

    return device_has_row_data(device) && row_start <= device->keymapShift && device->keymapShift < row_end;
}

static void decode_magkey_raw_values(const uint8_t *magkey_data, uint8_t bits_per_value, uint16_t magkey_raw_values[4]) {
    for (uint8_t value_index = 0; value_index < 4; value_index++) {
        for (uint8_t bit_index = 0; bit_index < bits_per_value; bit_index++) {
            uint16_t linear_bit  = (uint16_t)(value_index * bits_per_value + bit_index);
            uint8_t  byte_index  = (uint8_t)(linear_bit / 8);
            uint8_t  bit_in_byte = (uint8_t)(linear_bit % 8);
            if (magkey_data[byte_index] & (1u << bit_in_byte)) {
                magkey_raw_values[value_index] |= (uint16_t)(1u << bit_index);
            }
        }
    }
}

static bool read_magkey_device_data(uint16_t device_index, uint8_t current_row, uint16_t shift_value, const magkey_runtime_config_t *magkey_config, Keys_Data *data, bool *should_abort_row_scan) {
    const DeviceList *device                             = &deviceList[device_index];
    uint8_t           magkey_data[MAGKEY_DATA_BYTES_MAX] = {0};
    uint8_t           magkey_register                    = magkey_register_for_bytes(magkey_config->data_bytes);

    *should_abort_row_scan = false;
    if (!read_v2_magkey4_register(device->address, magkey_register, magkey_config->data_bytes, magkey_data)) {
        dprintf("failed to read MagKey4 data from addr 0x%02X!\n", device->address);
        *should_abort_row_scan = handle_i2c_read_result(device_index, data);
        return false;
    }

    uint16_t magkey_raw_values[4] = {0};
    decode_magkey_raw_values(magkey_data, magkey_config->bits_per_value, magkey_raw_values);

    uint8_t key_bits = 0;
    for (uint8_t key_index = 0; key_index < 4; key_index++) {
        uint16_t key_value = magkey_raw_values[key_index];
        uint8_t  col       = (uint8_t)(shift_value + key_index);
        bool     pressed   = magkey_update_pressed(device_index, key_index, key_value, current_row, col, magkey_config->value_shift);
        if (pressed) {
            key_bits |= (uint8_t)(1u << key_index);
        }
    }

    magkey_set_last_values(device_index, magkey_raw_values);
    data->type    = Pendant_v2_ModuleType_V2_MagKeys4;
    data->data[0] = key_bits;

    return true;
}

static bool read_device_row_data(uint16_t device_index, uint8_t current_row, uint16_t shift_value, const magkey_runtime_config_t *magkey_config, uint8_t *encoder_index, Keys_Data *data, bool *should_abort_row_scan) {
    const DeviceList *device = &deviceList[device_index];

    *should_abort_row_scan = false;
    switch (device->type) {
        case Pendant_v2_ModuleType_V1_PCA9557_Keys4:
        case Pendant_v2_ModuleType_V1_PCA9557_Keys5:
            *data = read_PCA9557_register(device->address);
            return true;
        case Pendant_v2_ModuleType_V1_XL9555_Keys4:
        case Pendant_v2_ModuleType_V1_XL9555_Keys5:
            *data = read_XL9555_register(device->address);
            return true;
        case Pendant_v2_ModuleType_V1_PCA9534A_RE:
            *data = read_PCA9534A_register(device->address);
            return true;
        case Pendant_v2_ModuleType_V2_Keys4:
            *data = read_v2_key4_register(device->address);
            return true;
        case Pendant_v2_ModuleType_V2_MagKeys4:
            return read_magkey_device_data(device_index, current_row, shift_value, magkey_config, data, should_abort_row_scan);
        case Pendant_v2_ModuleType_V2_RE:
            *data                            = read_v2_re_register(device->address);
            encoder_data[(*encoder_index)++] = data->data[0];
            return true;
        default:
            dprintf("failed to read data. unknown type: %d, ch: %X, addr: %X\n", device->type, device->ch, device->address);
            return false;
    }
}

static bool apply_device_row_data(const Keys_Data *data, uint16_t shift_value, matrix_row_t *row_data) {
    switch (data->type) {
        case Pendant_v2_ModuleType_V1_PCA9557_Keys4:
        case Pendant_v2_ModuleType_V1_PCA9557_Keys5:
        case Pendant_v2_ModuleType_V1_XL9555_Keys4:
        case Pendant_v2_ModuleType_V1_XL9555_Keys5:
        case Pendant_v2_ModuleType_V2_Keys4:
        case Pendant_v2_ModuleType_V2_MagKeys4:
            *row_data |= (matrix_row_t)data->data[0] << shift_value;
            return true;
        case Pendant_v2_ModuleType_V1_PCA9534A_RE:
        case Pendant_v2_ModuleType_V2_RE:
            *row_data |= (matrix_row_t)(((data->data[0] & 0x04) >> 2)) << shift_value;
            return true;
        default:
            return false;
    }
}

void keyboard_pre_init_kb(void) {
    pendant_led_init();

    // At first, find connected modules including OLED Display
    do_scan();

    keyboard_pre_init_user();
}

void keyboard_post_init_kb(void) {
    // debug_enable = true;
    // // debug_matrix = true;
    // // debug_keyboard=true;
    // debug_mouse = true;

    // eeconfig
    {
        if (!eeconfig_is_kb_datablock_valid()) {
            eeconfig_init_kb();
        }
        // initialize with the keyboard config data
        kb_config_load_from_eeprom();
    }

#ifdef POINTING_DEVICE_ENABLE
    keyboard_post_init_kb_trackball(kb_config_get());
#endif

    keyboard_post_init_kb_encoder(kb_config_get());
    {
        const kb_config_t *kb       = kb_config_get();
        uint8_t            led_mode = kb->v.general.led_base_mode;
        if (led_mode >= PendantLED_MODE_BASE_MAX) {
            GeneralConfigBits general = *kb_config_get_general();
            led_mode                  = PendantLED_MODE_BASE_Scanning;
            general.led_base_mode     = led_mode;
            kb_config_update_general(&general);
        }
        pendant_led_set_mode_base((PendantLED_MODE_BASE)led_mode);
    }

#ifdef OLED_ENABLE
    {
        const kb_config_t *kb           = kb_config_get();
        uint8_t            display_mode = kb->v.general.display_mode;
        // if the stored display mode is invalid or set to Keypress, reset to default
        if (display_mode >= DisplayMode_MAX || display_mode == DisplayMode_Keypress) {
            GeneralConfigBits general = *kb_config_get_general();
            display_mode              = DisplayMode_Info;
            general.display_mode      = display_mode;
            kb_config_update_general(&general);
        }
        display_set_mode((DisplayMode)display_mode);
    }
#endif

    keyboard_post_init_user();
}

void eeconfig_init_kb(void) {
    kb_config_reset_defaults();

    const kb_config_t *kb = kb_config_get();
    eeconfig_update_kb_datablock(&kb->raw, 0, sizeof(*kb));
    eeconfig_init_user();
}

void matrix_init_custom(void) {
    // initialize system pins
    ATOMIC_BLOCK_FORCEON {
        for (uint16_t i = 0; i < SYSTEM_KEY_NUM; i++) {
            uint16_t key_idx = SYSTEM_KEY_START_IDX + i;
            pin_t    dpin    = direct_pins[key_idx / MATRIX_COLS][key_idx % MATRIX_COLS];
            if (dpin != NO_PIN) gpio_set_pin_input_high(dpin);
        }
    }
}

void matrix_scan_kb(void) {
    matrix_scan_user();
}

void housekeeping_task_kb(void) {
    static uint32_t last_change_channel_error_log = 0;
    i2c_status_t    status                        = I2C_STATUS_SUCCESS;
    if (change_channel_get_last_failure(&status)) {
        if (last_change_channel_error_log == 0 || timer_elapsed32(last_change_channel_error_log) >= 5000) {
            dprintf("failed to change channel: %d!\n", (int)status);
            last_change_channel_error_log = timer_read32();
        }
    } else {
        last_change_channel_error_log = 0;
    }
    pendant_led_refresh(get_highest_layer(layer_state));
    housekeeping_task_user();
}

void matrix_read_cols_on_row(matrix_row_t current_matrix[], uint8_t current_row) {
    matrix_row_t            row_data      = read_system_row_data(current_row);
    const kb_config_t      *kb            = kb_config_get();
    magkey_runtime_config_t magkey_config = load_magkey_runtime_config(kb);

    uint8_t encoder_index = 0;
    for (uint16_t i = 0; i < nDevices; i++) {
        const DeviceList *device = &deviceList[i];
        if (!device_targets_row(device, current_row)) {
            continue;
        }

        uint16_t shift_value = device->keymapShift % MATRIX_COLS;

        if (!change_channel(device->ch)) {
            continue;
        }

        bool      should_abort_row_scan = false;
        Keys_Data data                  = {0};
        if (!read_device_row_data(i, current_row, shift_value, &magkey_config, &encoder_index, &data, &should_abort_row_scan)) {
            if (should_abort_row_scan) {
                current_matrix[current_row] = row_data;
                return;
            }
            continue;
        }

        if (!apply_device_row_data(&data, shift_value, &row_data)) {
            dprintf("failed to read data. type: %d, ch: %X, addr: %X, data: %X\n", device->type, device->ch, device->address, data.data[0]);
            if (handle_i2c_read_result(i, &data)) {
                current_matrix[current_row] = row_data;
                return;
            }
            continue;
        }
    }

    current_matrix[current_row] = row_data;
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
#ifdef POINTING_DEVICE_ENABLE
    trackball_wake_connected();
#endif

// If console is enabled, it will print the matrix position and status of each key pressed
#ifdef CONSOLE_ENABLE
    uprintf("KL: kc: 0x%04X, col: %2u, row: %2u, pressed: %u, time: %5u, int: %u, count: %u\n", keycode, record->event.key.col, record->event.key.row, record->event.pressed, record->event.time, record->tap.interrupted, record->tap.count);
#endif

#ifdef OLED_ENABLE
    if (!process_record_kb_display(keycode, record)) {
        return false;
    }
#endif

#ifdef POINTING_DEVICE_ENABLE
    // drag scroll
    if (!process_record_kb_trackball(kb_config_get(), keycode, record)) {
        return false;
    }
#endif

    // update random LED value on typing
    pendant_led_set_on_typing();

    return process_record_user(keycode, record);
}

bool get_permissive_hold(uint16_t keycode, keyrecord_t *record) {
    const kb_config_t *kb = kb_config_get();
    return kb->v.general.permissive_hold;
}

uint16_t get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    const kb_config_t *kb    = kb_config_get();
    uint16_t           value = 50 + kb->v.general.tapping_term_50ms * 25;
    return value;
}
