#include "magkey_utils.h"
#include "i2clib.h"
#include "kb_config.h"
#include "module_reader.h"
#include "device_scanner.h"
#include <string.h>

extern DeviceList deviceList[MAX_MCP_NUM];
extern uint16_t   nDevices;

static uint16_t magkey_last_values[MAX_MCP_NUM][4];
static bool     magkey_last_valid[MAX_MCP_NUM];
static bool     magkey_pressed[MAX_MCP_NUM][4];
static uint16_t magkey_rt_anchor[MAX_MCP_NUM][4];

static uint16_t magkey_calc_delta(uint16_t actuation, uint16_t reset) {
    uint16_t delta = (actuation >= reset) ? (actuation - reset) : (reset - actuation);
    return delta == 0 ? 1 : delta;
}

uint8_t magkey_data_bytes_from_config(uint8_t config_value) {
    switch (config_value) {
        case MagKey_12bits:
            return 6;
        case MagKey_8bits:
            return 4;
        case MagKey_4bits:
            return 2;
        case MagKey_2bits:
            return 1;
        default:
            return MAGKEY_DATA_BYTES_MAX;
    }
}

static uint8_t magkey_sanitize_data_bytes(uint8_t data_bytes) {
    switch (data_bytes) {
        case 6:
        case 4:
        case 2:
        case 1:
            return data_bytes;
        default:
            return MAGKEY_DATA_BYTES_MAX;
    }
}

uint8_t magkey_bits_per_value(uint8_t data_bytes) {
    uint8_t bytes = magkey_sanitize_data_bytes(data_bytes);
    return (uint8_t)((bytes * 8u) / 4u);
}

uint8_t magkey_value_shift(uint8_t bits_per_value) {
    return bits_per_value >= MAGKEY_REFERENCE_BITS ? 0 : (uint8_t)(MAGKEY_REFERENCE_BITS - bits_per_value);
}

uint8_t magkey_register_for_bytes(uint8_t data_bytes) {
    switch (magkey_sanitize_data_bytes(data_bytes)) {
        case 6:
            return Module_v2_MagKeyRegister_GET_DATA_6_BYTES;
        case 4:
            return Module_v2_MagKeyRegister_GET_DATA_4_BYTES;
        case 2:
            return Module_v2_MagKeyRegister_GET_DATA_2_BYTES;
        case 1:
            return Module_v2_MagKeyRegister_GET_DATA_1_BYTE;
        default:
            return Module_v2_MagKeyRegister_GET_DATA_6_BYTES;
    }
}

void magkey_reset_cached_values(void) {
    memset(magkey_last_valid, 0, sizeof(magkey_last_valid));
    memset(magkey_pressed, 0, sizeof(magkey_pressed));
    memset(magkey_rt_anchor, 0, sizeof(magkey_rt_anchor));
}

bool magkey_get_latest_value(uint8_t row, uint8_t col, uint16_t *out_value) {
    if (!out_value) {
        return false;
    }
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) {
        return false;
    }

    uint16_t key_idx = (uint16_t)row * MATRIX_COLS + col;
    for (uint16_t i = 0; i < nDevices; i++) {
        if (deviceList[i].type != Pendant_v2_ModuleType_V2_MagKeys4) {
            continue;
        }
        uint16_t shift = deviceList[i].keymapShift;
        if (key_idx < shift || key_idx >= (uint16_t)(shift + 4)) {
            continue;
        }
        uint8_t offset = (uint8_t)(key_idx - shift);
        *out_value     = magkey_last_valid[i] ? magkey_last_values[i][offset] : 0;
        return true;
    }

    return false;
}

bool magkey_update_pressed(uint16_t device_index, uint8_t key_index, uint16_t value, uint8_t row, uint8_t col, uint8_t value_shift) {
    const kb_config_t      *kb              = kb_config_get();
    const MagKeyConfigBits *cfg             = &kb->v.magkey[row][col];
    uint16_t                actuation_point = (uint16_t)(cfg->actualtion_point >> value_shift);
    uint16_t                reset_point     = (uint16_t)(cfg->reset_point >> value_shift);
    bool                    rapid_trigger   = cfg->rapid_trigger_enable;
    if (actuation_point < reset_point) {
        uint16_t tmp    = actuation_point;
        actuation_point = reset_point;
        reset_point     = tmp;
    }
    bool     press_increases = true;
    uint16_t delta           = magkey_calc_delta(actuation_point, reset_point);

    bool     pressed    = magkey_pressed[device_index][key_index];
    uint16_t anchor     = magkey_rt_anchor[device_index][key_index];
    bool     has_last   = magkey_last_valid[device_index];
    uint16_t prev_value = magkey_last_values[device_index][key_index];

    if (!has_last) {
        magkey_pressed[device_index][key_index]   = false;
        magkey_rt_anchor[device_index][key_index] = value;
        return false;
    }

    if (!rapid_trigger) {
        if (!pressed) {
            pressed = press_increases ? (value >= actuation_point) : (value <= actuation_point);
        } else {
            pressed = press_increases ? (value > reset_point) : (value < reset_point);
        }
        anchor = value;
    } else {
        bool in_rt_zone     = press_increases ? (value >= actuation_point) : (value <= actuation_point);
        bool was_in_rt_zone = has_last ? (press_increases ? (prev_value >= actuation_point) : (prev_value <= actuation_point)) : false;

        if (!in_rt_zone) {
            pressed = false;
            anchor  = value;
        } else if (!was_in_rt_zone) {
            pressed = true;
            anchor  = value;
        } else if (pressed) {
            if (press_increases) {
                if (value > anchor) {
                    anchor = value;
                }
                if (anchor >= delta && value <= (uint16_t)(anchor - delta)) {
                    pressed = false;
                    anchor  = value;
                }
            } else {
                if (value < anchor) {
                    anchor = value;
                }
                if (value >= (uint16_t)(anchor + delta)) {
                    pressed = false;
                    anchor  = value;
                }
            }
        } else {
            if (press_increases) {
                if (value < anchor) {
                    anchor = value;
                }
                if (value >= (uint16_t)(anchor + delta)) {
                    pressed = true;
                    anchor  = value;
                }
            } else {
                if (value > anchor) {
                    anchor = value;
                }
                if (anchor >= delta && value <= (uint16_t)(anchor - delta)) {
                    pressed = true;
                    anchor  = value;
                }
            }
        }
    }

    magkey_pressed[device_index][key_index]   = pressed;
    magkey_rt_anchor[device_index][key_index] = anchor;
    return pressed;
}

void magkey_set_last_values(uint16_t device_index, const uint16_t values[4]) {
    if (!values) {
        return;
    }
    for (uint8_t key_index = 0; key_index < 4; key_index++) {
        magkey_last_values[device_index][key_index] = values[key_index];
    }
    magkey_last_valid[device_index] = true;
}
