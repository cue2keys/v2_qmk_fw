#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "quantum.h"
#include "drivers/modular_pmw3610.h"

enum my_keycodes {
    // Next OLED Page
    NEXT_OLED_PAGE = QK_KB_0,
    // drag scroll feature for trackball
    DRAG_SCROLL,
};

enum MagKeyMode {
    MagKey_12bits = 0,
    MagKey_8bits  = 1,
    MagKey_4bits  = 2,
    MagKey_2bits  = 3,
    MagKey_Mode_MAX,
};

typedef struct __attribute__((packed)) {
    // Auto mouse layer (true/false)
    // 2^1=2, default: 1, max: 1
    bool mouse_layer_on : 1;

    // Auto mouse layer off delay time in ms (value * 100ms)
    // 2^6=64, default: 13, max: 63
    uint8_t mouse_layer_off_delay_ms : 6;

    // All keys are treated as mouse keys (true/false)
    // 2^1=2, default: 0, max: 1
    bool all_keys_are_mouse_keys : 1;

    // Trackball off timeout (never, 5min, 10min, 15 min)
    // 2^2=4, default: 1, max: 3
    uint8_t trackball_timeout : 2;

    // Rescan I2C devices after consecutive read errors (true/false)
    // 2^1=2, default: 1, max: 1
    bool rescan_i2c_on_read_error : 1;

    // Permissive hold (true/false)
    // 2^1=2, default: 1, max: 1
    bool permissive_hold : 1;

    // Tapping term (ms, 50-425, 50 + (value * 25) ms)
    // 2^4=16, default: 6, max: 15
    uint8_t tapping_term_50ms : 4;

    // Display mode (0: Info, 1: U1 walking, 2: keypress)
    // 2^2=4, default: 0, max: 2
    uint8_t display_mode : 2;

    // Pendant LED base mode (0: Off, 1: Layer, 2: RandomOnType, 3: Scanning)
    // 2^2=4, default: 3, max: 3
    uint8_t led_base_mode : 2;

    // MagKey data bytes to read (1/2/4/6)
    // 2^2=4, default: 0, valid: 0-3 (0: 12bits, 1: 8bits, 2: 4bits, 3: 2bits)
    uint8_t magkey_data_bytes : 2;

    // Dummy value for padding
    uint8_t _dummy : 2;
} GeneralConfigBits;
_Static_assert(sizeof(GeneralConfigBits) == 3, "GeneralConfigBits must fit into 3 bytes");

// to avoid warning `offset of packed bit-field 'drag_scroll_speed_magnification' has changed in GCC 4.4`, use uint32_t as base type
typedef struct __attribute__((packed)) {
    // Angle adjustment for trackballs (left)
    // 2^9=512, default: 0, max: 359
    uint32_t angle : 9;
    // Drag scroll speed magnification (value * 0.25)
    // 2^4=16, default: 4, max: 15
    uint32_t drag_scroll_speed_magnification : 4;

    // CPI step index (value + 1) * 200 (200..3200)
    // 2^4=16, default: 2 (600 CPI), max: 15 (3200 CPI)
    uint32_t cpi_step : 4;

    // Invert drag scroll X-axis direction (true/false)
    // 2^1=2, default: 0, max: 1
    bool invert_drag_scroll_x : 1;
    // Invert drag scroll Y-axis direction (true/false)
    // 2^1=2, default: 0, max: 1
    bool invert_drag_scroll_y : 1;

    // Dummy value for padding
    uint8_t _dummy : 5;
} TBConfigBits;
_Static_assert(sizeof(TBConfigBits) == 3, "TBConfigBits must fit into 4 bytes");

typedef struct __attribute__((packed)) {
    // Rotary encoder resolution (restart needed to reflect)
    // 2^2=4, default: 2, max: 3
    uint8_t resolution : 2;

    // Dummy value for padding
    uint8_t _dummy : 6;
} REConfigBits;
_Static_assert(sizeof(REConfigBits) == 1, "REConfigBits must fit into 1 byte");

typedef struct __attribute__((packed)) {
    // Actuation Point
    // 2^12=4096, default: 3000, min+ 0, max: 4096
    uint16_t actualtion_point : 12;

    // Reset Point (if rapid trigger is enabled, this value is the distance from actuation point)
    // 2^12=4096, default: 3000, min+ 0, max: 4096
    uint16_t reset_point : 12;

    // Rapid Trigger Enable (true/false)
    // 2^1=2, default: 0, max: 1
    bool rapid_trigger_enable : 1;

    // Dummy value for padding
    uint8_t _dummy : 7;
} MagKeyConfigBits;
_Static_assert(sizeof(MagKeyConfigBits) == 4, "MagKeyConfigBits must fit into 4 bytes");

typedef struct __attribute__((packed)) {
    GeneralConfigBits general;
    TBConfigBits      tb[NUM_MODULAR_PMW3610];
    REConfigBits      re[NUM_ENCODERS];
    MagKeyConfigBits  magkey[MATRIX_ROWS][MATRIX_COLS];
} EeConfigView;
_Static_assert(sizeof(EeConfigView) == 1311, "EeConfigView must fit into 1311 bytes");

typedef union {
    uint8_t      raw[EECONFIG_KB_DATA_SIZE]; // 2KB
    EeConfigView v;
} kb_config_t;
_Static_assert(sizeof(kb_config_t) == EECONFIG_KB_DATA_SIZE, "kb_config_t must fit into 2048 bytes");

// NOTE:
// - Direct assignment to the config is disallowed; use the update/edit APIs below.
// - Access should go through kb_config_get() or the range getters.
const kb_config_t *kb_config_get(void);

void kb_config_reset_defaults(void);
void kb_config_load_from_eeprom(void);

const GeneralConfigBits *kb_config_get_general(void);
const TBConfigBits      *kb_config_get_tb(uint8_t index);
const REConfigBits      *kb_config_get_re(uint8_t index);
const MagKeyConfigBits  *kb_config_get_magkey(uint8_t row, uint8_t col);

void kb_config_update_general(const GeneralConfigBits *src);
void kb_config_update_tb(uint8_t index, const TBConfigBits *src);
void kb_config_update_re(uint8_t index, const REConfigBits *src);
void kb_config_update_magkey(uint8_t row, uint8_t col, const MagKeyConfigBits *src);

void kb_config_edit_general(void (*fn)(GeneralConfigBits *kb_cfg, void *ctx), void *ctx);
void kb_config_edit_tb(uint8_t index, void (*fn)(TBConfigBits *kb_cfg, void *ctx), void *ctx);
void kb_config_edit_re(uint8_t index, void (*fn)(REConfigBits *kb_cfg, void *ctx), void *ctx);
void kb_config_edit_magkey(uint8_t row, uint8_t col, void (*fn)(MagKeyConfigBits *kb_cfg, void *ctx), void *ctx);

void kb_config_mark_dirty_full(void);

void debug_output_kb_config(const kb_config_t *kb_cfg);
bool process_kb_config_modification(const kb_config_t *kb_cfg, uint16_t keycode, keyrecord_t *record);
