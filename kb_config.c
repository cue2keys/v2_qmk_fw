#include <stddef.h>
#include <string.h>

#include "kb_config.h"
#include "drivers/modular_pmw3610.h"
#include "pendant_led.h"
#include "eeconfig.h"
#include "deferred_exec.h"

#ifdef kb_config
#    undef kb_config
#endif

static kb_config_t kb_config_storage;

#define SAVE_DEFER_DELAY_MS 1000

typedef struct {
    bool           dirty_full;
    bool           dirty_general;
    bool           dirty_tb[NUM_MODULAR_PMW3610];
    bool           dirty_re[NUM_ENCODERS];
    bool           dirty_magkey[MATRIX_ROWS][MATRIX_COLS];
    deferred_token token;
} kb_config_dirty_state_t;

static kb_config_dirty_state_t kb_dirty = {0};

static uint32_t save_param_deferred_cb(uint32_t trigger_time, void *cb_arg);

static bool tb_index_valid(uint8_t index) {
    return index < NUM_MODULAR_PMW3610;
}

static bool re_index_valid(uint8_t index) {
    return index < NUM_ENCODERS;
}

static bool magkey_coord_valid(uint8_t row, uint8_t col) {
    return row < MATRIX_ROWS && col < MATRIX_COLS;
}

static void reset_dirty_state(void) {
    kb_dirty.dirty_full    = false;
    kb_dirty.dirty_general = false;
    memset(kb_dirty.dirty_tb, 0, sizeof(kb_dirty.dirty_tb));
    memset(kb_dirty.dirty_re, 0, sizeof(kb_dirty.dirty_re));
    memset(kb_dirty.dirty_magkey, 0, sizeof(kb_dirty.dirty_magkey));
    kb_dirty.token = INVALID_DEFERRED_TOKEN;
}

static void schedule_save_param(void) {
    if (kb_dirty.token == INVALID_DEFERRED_TOKEN) {
        kb_dirty.token = defer_exec(SAVE_DEFER_DELAY_MS, save_param_deferred_cb, NULL);
    } else {
        extend_deferred_exec(kb_dirty.token, SAVE_DEFER_DELAY_MS);
    }
}

static void note_general_change(void) {
    kb_dirty.dirty_general = true;
    schedule_save_param();
}

static void note_tb_change(uint8_t index) {
    if (!tb_index_valid(index)) {
        return;
    }

    kb_dirty.dirty_tb[index] = true;
    schedule_save_param();
}

static void note_re_change(uint8_t index) {
    if (!re_index_valid(index)) {
        return;
    }

    kb_dirty.dirty_re[index] = true;
    schedule_save_param();
}

static void note_magkey_change(uint8_t row, uint8_t col) {
    if (!magkey_coord_valid(row, col)) {
        return;
    }

    kb_dirty.dirty_magkey[row][col] = true;
    schedule_save_param();
}

static void note_full_change(void) {
    kb_dirty.dirty_full = true;
    schedule_save_param();
}

static void write_tb_if_dirty(const kb_config_t *kb, uint8_t index) {
    if (!kb_dirty.dirty_tb[index]) {
        return;
    }

    size_t off = offsetof(EeConfigView, tb) + (index * sizeof(TBConfigBits));
    eeconfig_update_kb_datablock(&kb->v.tb[index], off, sizeof(TBConfigBits));
}

static void write_re_if_dirty(const kb_config_t *kb, uint8_t index) {
    if (!kb_dirty.dirty_re[index]) {
        return;
    }

    size_t off = offsetof(EeConfigView, re) + (index * sizeof(REConfigBits));
    eeconfig_update_kb_datablock(&kb->v.re[index], off, sizeof(REConfigBits));
}

static void write_magkey_if_dirty(const kb_config_t *kb, uint8_t row, uint8_t col) {
    if (!kb_dirty.dirty_magkey[row][col]) {
        return;
    }

    size_t off = offsetof(EeConfigView, magkey) + (((size_t)row * MATRIX_COLS + col) * sizeof(MagKeyConfigBits));
    eeconfig_update_kb_datablock(&kb->v.magkey[row][col], off, sizeof(MagKeyConfigBits));
}

static uint32_t save_param_deferred_cb(uint32_t trigger_time, void *cb_arg) {
    (void)trigger_time;
    (void)cb_arg;

    const kb_config_t *kb = kb_config_get();
    if (kb_dirty.dirty_full) {
        eeconfig_update_kb_datablock(&kb->raw, 0, sizeof(*kb));
    } else {
        if (kb_dirty.dirty_general) {
            eeconfig_update_kb_datablock(&kb->v.general, offsetof(EeConfigView, general), sizeof(GeneralConfigBits));
        }
        for (uint8_t i = 0; i < NUM_MODULAR_PMW3610; i++) {
            write_tb_if_dirty(kb, i);
        }
        for (uint8_t i = 0; i < NUM_ENCODERS; i++) {
            write_re_if_dirty(kb, i);
        }
        for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
            for (uint8_t col = 0; col < MATRIX_COLS; col++) {
                write_magkey_if_dirty(kb, row, col);
            }
        }
    }

    reset_dirty_state();
    return 0;
}

const kb_config_t *kb_config_get(void) {
    return &kb_config_storage;
}

const GeneralConfigBits *kb_config_get_general(void) {
    return &kb_config_storage.v.general;
}

const TBConfigBits *kb_config_get_tb(uint8_t index) {
    if (!tb_index_valid(index)) {
        return NULL;
    }
    return &kb_config_storage.v.tb[index];
}

const REConfigBits *kb_config_get_re(uint8_t index) {
    if (!re_index_valid(index)) {
        return NULL;
    }
    return &kb_config_storage.v.re[index];
}

const MagKeyConfigBits *kb_config_get_magkey(uint8_t row, uint8_t col) {
    if (!magkey_coord_valid(row, col)) {
        return NULL;
    }
    return &kb_config_storage.v.magkey[row][col];
}

static void kb_config_init_defaults(kb_config_t *kb_config) {
    for (uint8_t i = 0; i < NUM_MODULAR_PMW3610; i++) {
        kb_config->v.tb[i].angle                           = 0;
        kb_config->v.tb[i].invert_drag_scroll_x            = false;
        kb_config->v.tb[i].invert_drag_scroll_y            = false;
        kb_config->v.tb[i].drag_scroll_speed_magnification = 3;
        kb_config->v.tb[i].cpi_step                        = (PMW3610_DEFAULT_CPI / 200) - 1;
    }
    kb_config->v.general.mouse_layer_on           = true;
    kb_config->v.general.mouse_layer_off_delay_ms = 13;
    kb_config->v.general.all_keys_are_mouse_keys  = false;
    kb_config->v.general.trackball_timeout        = 0;
    kb_config->v.general.rescan_i2c_on_read_error = true;
    kb_config->v.general.permissive_hold          = true;
    kb_config->v.general.tapping_term_50ms        = 6;
    kb_config->v.general.led_base_mode            = PendantLED_MODE_BASE_Layer;
    kb_config->v.general.magkey_data_bytes        = MagKey_12bits;

    for (uint8_t i = 0; i < NUM_ENCODERS; i++) {
        kb_config->v.re[i].resolution = 2;
    }

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            kb_config->v.magkey[row][col].actualtion_point     = 3551;
            kb_config->v.magkey[row][col].reset_point          = 2929;
            kb_config->v.magkey[row][col].rapid_trigger_enable = false;
        }
    }
}

void kb_config_reset_defaults(void) {
    kb_config_init_defaults(&kb_config_storage);
}

void kb_config_load_from_eeprom(void) {
    eeconfig_read_kb_datablock(&kb_config_storage.raw, 0, sizeof(kb_config_storage));
}

void kb_config_update_general(const GeneralConfigBits *src) {
    if (src == NULL) {
        return;
    }
    kb_config_storage.v.general = *src;
    note_general_change();
}

void kb_config_update_tb(uint8_t index, const TBConfigBits *src) {
    if (src == NULL || !tb_index_valid(index)) {
        return;
    }
    kb_config_storage.v.tb[index] = *src;
    note_tb_change(index);
}

void kb_config_update_re(uint8_t index, const REConfigBits *src) {
    if (src == NULL || !re_index_valid(index)) {
        return;
    }
    kb_config_storage.v.re[index] = *src;
    note_re_change(index);
}

void kb_config_update_magkey(uint8_t row, uint8_t col, const MagKeyConfigBits *src) {
    if (src == NULL || !magkey_coord_valid(row, col)) {
        return;
    }
    kb_config_storage.v.magkey[row][col] = *src;
    note_magkey_change(row, col);
}

void kb_config_edit_general(void (*fn)(GeneralConfigBits *kb_cfg, void *ctx), void *ctx) {
    if (fn == NULL) {
        return;
    }

    fn(&kb_config_storage.v.general, ctx);
    note_general_change();
}

void kb_config_edit_tb(uint8_t index, void (*fn)(TBConfigBits *kb_cfg, void *ctx), void *ctx) {
    if (fn == NULL || !tb_index_valid(index)) {
        return;
    }

    fn(&kb_config_storage.v.tb[index], ctx);
    note_tb_change(index);
}

void kb_config_edit_re(uint8_t index, void (*fn)(REConfigBits *kb_cfg, void *ctx), void *ctx) {
    if (fn == NULL || !re_index_valid(index)) {
        return;
    }

    fn(&kb_config_storage.v.re[index], ctx);
    note_re_change(index);
}

void kb_config_edit_magkey(uint8_t row, uint8_t col, void (*fn)(MagKeyConfigBits *kb_cfg, void *ctx), void *ctx) {
    if (fn == NULL || !magkey_coord_valid(row, col)) {
        return;
    }

    fn(&kb_config_storage.v.magkey[row][col], ctx);
    note_magkey_change(row, col);
}

void kb_config_mark_dirty_full(void) {
    note_full_change();
}
