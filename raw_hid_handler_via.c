#include QMK_KEYBOARD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "raw_hid.h"
#include "raw_hid_handler_via.h"
#include "i2clib.h"
#include "device_scanner.h"
#include "trackball.h"
#include "display.h"
#include "matrix.h"
#include "cue2keys.h"
#include "magkey_utils.h"
#include "./drivers/encoder_dynamic_res.h"
#include "pendant_builder.h"
#include "pendant_led.h"
#include "flatcc/flatcc_endian.h"

#ifdef VIA_ENABLE

extern DeviceList deviceList[MAX_MCP_NUM];
extern uint16_t   nDevices;

#    define DEVICEITEM_BUF_SIZE ((sizeof(Pendant_v2_DeviceItem_t)) * MAX_MCP_NUM)
#    define TRACKBALL_CONNECTED_SLOT_COUNT 4

typedef uint32_t (*ValueGetter)(void);
typedef void (*ValueSetter)(uint32_t value);

typedef enum {
    COMMAND_RESULT_UNHANDLED,
    COMMAND_RESULT_HANDLED,
    COMMAND_RESULT_ABORT,
} command_result_t;

struct ValueAccessor {
    ValueGetter get;
    ValueSetter set;
};

static const struct ValueAccessor *via_get_accessors(void);

typedef enum {
    GENERAL_FIELD_MOUSE_LAYER_ON,
    GENERAL_FIELD_MOUSE_LAYER_OFF_DELAY_MS,
    GENERAL_FIELD_ALL_KEYS_ARE_MOUSE_KEYS,
    GENERAL_FIELD_TRACKBALL_TIMEOUT,
    GENERAL_FIELD_RESCAN_I2C_ON_READ_ERROR,
    GENERAL_FIELD_PERMISSIVE_HOLD,
    GENERAL_FIELD_TAPPING_TERM_50MS,
    GENERAL_FIELD_DISPLAY_MODE,
    GENERAL_FIELD_LED_BASE_MODE,
    GENERAL_FIELD_MAGKEY_DATA_BYTES,
} general_field_t;

typedef enum {
    TB_FIELD_ANGLE,
    TB_FIELD_DRAG_SCROLL_SPEED_MAGNIFICATION,
    TB_FIELD_CPI_STEP,
    TB_FIELD_INVERT_DRAG_SCROLL_X,
    TB_FIELD_INVERT_DRAG_SCROLL_Y,
} tb_field_t;

static inline uint32_t clamp_u32(uint32_t v, uint32_t max) {
    return v > max ? max : v;
}

static inline bool magkey_config_available(uint8_t row, uint8_t col) {
    uint16_t dummy = 0;
    return magkey_get_latest_value(row, col, &dummy);
}

static uint32_t get_general_field(general_field_t field) {
    const GeneralConfigBits *general = kb_config_get_general();

    switch (field) {
        case GENERAL_FIELD_MOUSE_LAYER_ON:
            return general->mouse_layer_on;
        case GENERAL_FIELD_MOUSE_LAYER_OFF_DELAY_MS:
            return general->mouse_layer_off_delay_ms;
        case GENERAL_FIELD_ALL_KEYS_ARE_MOUSE_KEYS:
            return general->all_keys_are_mouse_keys;
        case GENERAL_FIELD_TRACKBALL_TIMEOUT:
            return general->trackball_timeout;
        case GENERAL_FIELD_RESCAN_I2C_ON_READ_ERROR:
            return general->rescan_i2c_on_read_error;
        case GENERAL_FIELD_PERMISSIVE_HOLD:
            return general->permissive_hold;
        case GENERAL_FIELD_TAPPING_TERM_50MS:
            return general->tapping_term_50ms;
        case GENERAL_FIELD_DISPLAY_MODE:
            return general->display_mode;
        case GENERAL_FIELD_LED_BASE_MODE:
            return general->led_base_mode;
        case GENERAL_FIELD_MAGKEY_DATA_BYTES:
            return general->magkey_data_bytes;
    }

    return 0;
}

static void set_general_field(general_field_t field, uint32_t value) {
    const GeneralConfigBits *cur = kb_config_get_general();
    if (cur == NULL) {
        return;
    }

    GeneralConfigBits general = *cur;
    switch (field) {
        case GENERAL_FIELD_MOUSE_LAYER_ON:
            general.mouse_layer_on = value ? 1u : 0u;
            break;
        case GENERAL_FIELD_MOUSE_LAYER_OFF_DELAY_MS:
            general.mouse_layer_off_delay_ms = (uint8_t)value;
            break;
        case GENERAL_FIELD_ALL_KEYS_ARE_MOUSE_KEYS:
            general.all_keys_are_mouse_keys = value ? 1u : 0u;
            break;
        case GENERAL_FIELD_TRACKBALL_TIMEOUT:
            general.trackball_timeout = (uint8_t)value;
            break;
        case GENERAL_FIELD_RESCAN_I2C_ON_READ_ERROR:
            general.rescan_i2c_on_read_error = value ? 1u : 0u;
            break;
        case GENERAL_FIELD_PERMISSIVE_HOLD:
            general.permissive_hold = value ? 1u : 0u;
            break;
        case GENERAL_FIELD_TAPPING_TERM_50MS:
            general.tapping_term_50ms = (uint8_t)value;
            break;
        case GENERAL_FIELD_DISPLAY_MODE:
            general.display_mode = (uint8_t)value;
            break;
        case GENERAL_FIELD_LED_BASE_MODE:
            general.led_base_mode = (uint8_t)value;
            break;
        case GENERAL_FIELD_MAGKEY_DATA_BYTES:
            general.magkey_data_bytes = (uint8_t)value;
            break;
    }

    kb_config_update_general(&general);
}

static uint32_t get_tb_field(uint8_t index, tb_field_t field) {
    const TBConfigBits *tb = kb_config_get_tb(index);
    if (tb == NULL) {
        return 0;
    }

    switch (field) {
        case TB_FIELD_ANGLE:
            return tb->angle;
        case TB_FIELD_DRAG_SCROLL_SPEED_MAGNIFICATION:
            return tb->drag_scroll_speed_magnification;
        case TB_FIELD_CPI_STEP:
            return tb->cpi_step;
        case TB_FIELD_INVERT_DRAG_SCROLL_X:
            return tb->invert_drag_scroll_x;
        case TB_FIELD_INVERT_DRAG_SCROLL_Y:
            return tb->invert_drag_scroll_y;
    }

    return 0;
}

static void set_tb_field(uint8_t index, tb_field_t field, uint32_t value) {
    const TBConfigBits *cur = kb_config_get_tb(index);
    if (cur == NULL) {
        return;
    }

    TBConfigBits tb = *cur;
    switch (field) {
        case TB_FIELD_ANGLE:
            tb.angle = (uint16_t)value;
            break;
        case TB_FIELD_DRAG_SCROLL_SPEED_MAGNIFICATION:
            tb.drag_scroll_speed_magnification = (uint8_t)value;
            break;
        case TB_FIELD_CPI_STEP:
            tb.cpi_step = (uint8_t)value;
            break;
        case TB_FIELD_INVERT_DRAG_SCROLL_X:
            tb.invert_drag_scroll_x = value ? 1u : 0u;
            break;
        case TB_FIELD_INVERT_DRAG_SCROLL_Y:
            tb.invert_drag_scroll_y = value ? 1u : 0u;
            break;
    }

    kb_config_update_tb(index, &tb);
}

static uint32_t get_re_resolution(uint8_t index) {
    const REConfigBits *re = kb_config_get_re(index);
    return re == NULL ? 0 : re->resolution;
}

static void set_re_resolution(uint8_t index, uint32_t value) {
    const REConfigBits *cur = kb_config_get_re(index);
    if (cur == NULL) {
        return;
    }

    REConfigBits re = *cur;
    re.resolution   = (uint8_t)value;
    kb_config_update_re(index, &re);
}

static void update_magkey_config(uint8_t row, uint8_t col, uint16_t actuation, uint16_t reset, bool rapid) {
    const MagKeyConfigBits *cur = kb_config_get_magkey(row, col);
    if (cur == NULL) {
        return;
    }

    MagKeyConfigBits cfg     = *cur;
    cfg.actualtion_point     = actuation;
    cfg.reset_point          = reset;
    cfg.rapid_trigger_enable = rapid ? 1u : 0u;
    kb_config_update_magkey(row, col, &cfg);
}

static size_t build_deviceitem_payload(uint8_t *buf, size_t buf_size) {
    const uint16_t dev_count = (nDevices > MAX_MCP_NUM) ? MAX_MCP_NUM : nDevices;
    const size_t   payload_n = (size_t)dev_count * sizeof(Pendant_v2_DeviceItem_t);
    if (payload_n > buf_size) {
        return 0;
    }

    memset(buf, 0, buf_size);

    for (uint16_t i = 0; i < dev_count; i++) {
        const DeviceList       *d     = &deviceList[i];
        const uint8_t           shift = (d->keymapShift > 0xFFu) ? 0xFFu : (uint8_t)d->keymapShift;
        Pendant_v2_DeviceItem_t tmp;
        Pendant_v2_DeviceItem_assign_to_pe(&tmp, d->ch, d->address, (Pendant_v2_ModuleType_enum_t)d->type, shift, d->UID);
        memcpy(buf + (i * sizeof(Pendant_v2_DeviceItem_t)), &tmp, sizeof(Pendant_v2_DeviceItem_t));
    }

    return payload_n;
}

static void mark_unhandled_command(Pendant_v2_Pkt_t *pkt) {
    pkt->headers.command_id = id_unhandled;
}

static void clear_response_payload(Pendant_v2_Pkt_t *pkt) {
    pkt->headers.data_length = 0;
}

static uint32_t read_packet_u32(const Pendant_v2_Pkt_t *pkt) {
    return ((uint32_t)pkt->data[0]) | ((uint32_t)pkt->data[1] << 8) | ((uint32_t)pkt->data[2] << 16) | ((uint32_t)pkt->data[3] << 24);
}

static void write_packet_u32(Pendant_v2_Pkt_t *pkt, uint32_t value) {
    pkt->headers.data_length = 4;
    __flatbuffers_uoffset_write_to_pe(pkt->data, (flatbuffers_uoffset_t)value);
}

static bool matrix_coord_valid(uint8_t row, uint8_t col) {
    return row < MATRIX_ROWS && col < MATRIX_COLS;
}

static void encode_get_keypress_response(Pendant_v2_Pkt_t *pkt, uint16_t value, bool is_magkey) {
    Pendant_v2_GetKeypressResponse_t resp_pe;
    memset(&resp_pe, 0, sizeof(resp_pe));
    pkt->headers.data_length = (uint8_t)sizeof(Pendant_v2_GetKeypressResponse_t);
    Pendant_v2_GetKeypressResponse_assign_to_pe(&resp_pe, value, is_magkey ? 1 : 0);
    memcpy(pkt->data, &resp_pe, sizeof(resp_pe));
}

static void encode_magkey_config_response(Pendant_v2_Pkt_t *pkt, const MagKeyConfigBits *cfg) {
    uint16_t actuation = cfg->actualtion_point;
    uint16_t reset     = cfg->reset_point;

    pkt->data[0]             = (uint8_t)(actuation & 0xFFu);
    pkt->data[1]             = (uint8_t)((actuation >> 8) & 0xFFu);
    pkt->data[2]             = (uint8_t)(reset & 0xFFu);
    pkt->data[3]             = (uint8_t)((reset >> 8) & 0xFFu);
    pkt->data[4]             = cfg->rapid_trigger_enable ? 1u : 0u;
    pkt->headers.data_length = 5;
}

static void encode_trackball_connected_response(Pendant_v2_Pkt_t *pkt) {
    uint8_t flags[TRACKBALL_CONNECTED_SLOT_COUNT] = {0};
    uint8_t count                                 = 0;

#    ifdef POINTING_DEVICE_ENABLE
    count = trackball_get_connected_flags(flags, ARRAY_SIZE(flags));
#    endif
    count = (uint8_t)clamp_u32(count, ARRAY_SIZE(flags));

    Pendant_v2_GetTrackballConnectedResponse_t resp_pe;
    memset(&resp_pe, 0, sizeof(resp_pe));
    pkt->headers.data_length = (uint8_t)sizeof(Pendant_v2_GetTrackballConnectedResponse_t);
    Pendant_v2_GetTrackballConnectedResponse_assign_to_pe(&resp_pe, count, flags);
    memcpy(pkt->data, &resp_pe, sizeof(resp_pe));
}

static void encode_i2c_address_update_response(Pendant_v2_Pkt_t *pkt, Pendant_v2_SetI2CAddrCmdErrorCode_enum_t error_code) {
    Pendant_v2_SetI2CAddrCmdResponse_t resp_pe;
    pkt->headers.data_length = (uint8_t)sizeof(Pendant_v2_SetI2CAddrCmdResponse_t);
    Pendant_v2_SetI2CAddrCmdResponse_assign_to_pe(&resp_pe, error_code == Pendant_v2_SetI2CAddrCmdErrorCode_SUCCESS ? 1 : 0, error_code);
    memcpy(pkt->data, &resp_pe, sizeof(resp_pe));
}

static bool handle_get_fw_version(Pendant_v2_Pkt_t *pkt) {
    const char *fw_version = FW_VERSION;
    size_t      fw_len     = strlen(fw_version);
    size_t      copy_len   = fw_len < sizeof(pkt->data) ? fw_len : sizeof(pkt->data);

    pkt->headers.data_length = (uint8_t)copy_len;
    memcpy(pkt->data, fw_version, copy_len);
    return true;
}

static bool handle_get_keypress_command(Pendant_v2_Pkt_t *pkt) {
    if (pkt->headers.data_length < (uint8_t)sizeof(Pendant_v2_GetKeypressCmd_t)) {
        dprintf("[get_param] keypress payload too short: %u\n", pkt->headers.data_length);
        clear_response_payload(pkt);
        return true;
    }

    Pendant_v2_GetKeypressCmd_t *data = (Pendant_v2_GetKeypressCmd_t *)(void *)(pkt->data);
    uint8_t                      row  = data->row;
    uint8_t                      col  = data->col;
    if (!matrix_coord_valid(row, col)) {
        dprintf("[get_param] keypress out of range: row=%u col=%u\n", row, col);
        clear_response_payload(pkt);
        return true;
    }

    uint16_t value     = 0;
    bool     is_magkey = magkey_get_latest_value(row, col, &value);
    if (!is_magkey) {
        value = matrix_is_on(row, col) ? 1u : 0u;
    }

    encode_get_keypress_response(pkt, value, is_magkey);
    return true;
}

static bool handle_get_magkey_config_command(Pendant_v2_Pkt_t *pkt) {
    if (pkt->headers.data_length < 2) {
        dprintf("[get_param] magkey payload too short: %u\n", pkt->headers.data_length);
        clear_response_payload(pkt);
        return true;
    }

    uint8_t row = pkt->data[0];
    uint8_t col = pkt->data[1];
    if (!matrix_coord_valid(row, col)) {
        dprintf("[get_param] magkey out of range: row=%u col=%u\n", row, col);
        clear_response_payload(pkt);
        return true;
    }
    if (!magkey_config_available(row, col)) {
        dprintf("[get_param] magkey not found: row=%u col=%u\n", row, col);
        clear_response_payload(pkt);
        return true;
    }

    encode_magkey_config_response(pkt, &kb_config_get()->v.magkey[row][col]);
    return true;
}

static bool handle_get_command(Pendant_v2_Pkt_t *pkt, uint8_t value_id) {
    switch (value_id) {
        case Pendant_v2_HIDCommandValueID_get_fw_version:
            return handle_get_fw_version(pkt);
        case Pendant_v2_HIDCommandValueID_get_device_list:
            dprintf("[get_param] send device info\n");
            send_device_list(pkt);
            return true;
        case Pendant_v2_HIDCommandValueID_get_keypress:
            return handle_get_keypress_command(pkt);
        case Pendant_v2_HIDCommandValueID_get_magkey_config:
            return handle_get_magkey_config_command(pkt);
        case Pendant_v2_HIDCommandValueID_get_trackball_connected:
            encode_trackball_connected_response(pkt);
            return true;
        default:
            return false;
    }
}

static command_result_t handle_set_update_i2c_address(Pendant_v2_Pkt_t *pkt) {
    Pendant_v2_SetI2CAddrCmd_t *data = (Pendant_v2_SetI2CAddrCmd_t *)(void *)(pkt->data);
    dprintf("Pendant_v2_SetI2CAddrCmd_struct_t ch=%d old_addr=0x%02X new_addr=0x%02X\n", data->ch, data->old_addr, data->new_addr);

    Pendant_v2_SetI2CAddrCmdErrorCode_enum_t error_code = update_v2_address(data->ch, data->old_addr, data->new_addr);
    encode_i2c_address_update_response(pkt, error_code);

    if (error_code == Pendant_v2_SetI2CAddrCmdErrorCode_SUCCESS) {
        wait_ms(100);
        do_scan();
    }

    return COMMAND_RESULT_HANDLED;
}

static command_result_t handle_set_magkey_config_command(Pendant_v2_Pkt_t *pkt) {
    if (pkt->headers.data_length < (uint8_t)sizeof(Pendant_v2_SetMagkeyConfigCmd_t)) {
        dprintf("[set_param] magkey payload too short: %u\n", pkt->headers.data_length);
        return COMMAND_RESULT_ABORT;
    }

    Pendant_v2_SetMagkeyConfigCmd_t data;
    memcpy(&data, pkt->data, sizeof(data));

    uint8_t row = data.row;
    uint8_t col = data.col;
    if (!matrix_coord_valid(row, col)) {
        dprintf("[set_param] magkey out of range: row=%u col=%u\n", row, col);
        return COMMAND_RESULT_ABORT;
    }
    if (!magkey_config_available(row, col)) {
        dprintf("[set_param] magkey not found: row=%u col=%u\n", row, col);
        return COMMAND_RESULT_ABORT;
    }

    uint16_t actuation = 0;
    uint16_t reset     = 0;
    memcpy(&actuation, &data.actuation, sizeof(actuation));
    memcpy(&reset, &data.reset, sizeof(reset));
    actuation = (uint16_t)clamp_u32(actuation, 0x0FFFu);
    reset     = (uint16_t)clamp_u32(reset, 0x0FFFu);

    bool rapid = data.rapid_trigger_enable != 0;
    dprintf("[set_param] magkey row=%u col=%u actuation=%u reset=%u rapid=%u\n", row, col, actuation, reset, rapid ? 1 : 0);
    update_magkey_config(row, col, actuation, reset, rapid);
    return COMMAND_RESULT_HANDLED;
}

static command_result_t handle_set_display_keypress_target_command(Pendant_v2_Pkt_t *pkt) {
    if (pkt->headers.data_length < (uint8_t)sizeof(Pendant_v2_SetDisplayKeypressTargetCmd_t)) {
        dprintf("[set_param] display keypress target payload too short: %u\n", pkt->headers.data_length);
        return COMMAND_RESULT_ABORT;
    }

    Pendant_v2_SetDisplayKeypressTargetCmd_t data;
    memcpy(&data, pkt->data, sizeof(data));
    display_set_keypress_target(data.keypress_row, data.keypress_col);
    return COMMAND_RESULT_HANDLED;
}

static command_result_t handle_set_command(Pendant_v2_Pkt_t *pkt, uint8_t value_id) {
    switch (value_id) {
        case Pendant_v2_HIDCommandValueID_update_i2c_address:
            return handle_set_update_i2c_address(pkt);
        case Pendant_v2_HIDCommandValueID_reset_all_params:
            kb_config_reset_defaults();
            kb_config_mark_dirty_full();
            return COMMAND_RESULT_HANDLED;
        case Pendant_v2_HIDCommandValueID_rescan_i2c_devices:
            do_scan();
            return COMMAND_RESULT_HANDLED;
        case Pendant_v2_HIDCommandValueID_set_magkey_config:
            return handle_set_magkey_config_command(pkt);
        case Pendant_v2_HIDCommandValueID_set_display_keypress_target:
            return handle_set_display_keypress_target_command(pkt);
        default:
            return COMMAND_RESULT_UNHANDLED;
    }
}

static bool handle_get_kbc_param(Pendant_v2_Pkt_t *pkt, uint8_t value_id) {
    if (!Pendant_v2_KBC_ParamID_is_known_value(value_id)) {
        return false;
    }

    const struct ValueAccessor accessor = via_get_accessors()[value_id];
    if (accessor.get == NULL) {
        dprintf("[get_param] missing accessor for value_id=%u\n", value_id);
        clear_response_payload(pkt);
        return true;
    }

    uint32_t value = accessor.get();
    dprintf("[get_param] %s v: %d\n", Pendant_v2_KBC_ParamID_name(value_id), (int)value);
    write_packet_u32(pkt, value);
    return true;
}

static command_result_t handle_set_kbc_param(Pendant_v2_Pkt_t *pkt, uint8_t value_id) {
    if (!Pendant_v2_KBC_ParamID_is_known_value(value_id)) {
        return COMMAND_RESULT_UNHANDLED;
    }

    const struct ValueAccessor accessor = via_get_accessors()[value_id];
    if (accessor.set == NULL) {
        dprintf("[set_param] missing accessor for value_id=%u\n", value_id);
        return COMMAND_RESULT_ABORT;
    }

    uint32_t value = read_packet_u32(pkt);
    dprintf("[set_param] %s v: %d\n", Pendant_v2_KBC_ParamID_name(value_id), (int)value);
    accessor.set(value);
    return COMMAND_RESULT_HANDLED;
}

#    define DECL_VALUE_ACCESSOR(ID, GET_BODY, SET_BODY) static uint32_t get_##ID(void) GET_BODY static void set_##ID(uint32_t value) SET_BODY static const struct ValueAccessor accessor_##ID = {.get = get_##ID, .set = set_##ID}
#    define GENERAL_GET_BODY(FIELD)          \
        {                                    \
            return get_general_field(FIELD); \
        }
#    define GENERAL_SET_BODY(FIELD, LIMIT)                     \
        {                                                      \
            set_general_field(FIELD, clamp_u32(value, LIMIT)); \
        }
#    define TB_GET_BODY(INDEX, FIELD)          \
        {                                      \
            return get_tb_field(INDEX, FIELD); \
        }
#    define TB_SET_BODY(INDEX, FIELD, LIMIT)                     \
        {                                                        \
            set_tb_field(INDEX, FIELD, clamp_u32(value, LIMIT)); \
        }
#    define RE_GET_BODY(INDEX)               \
        {                                    \
            return get_re_resolution(INDEX); \
        }
#    define RE_SET_BODY(INDEX, LIMIT)                          \
        {                                                      \
            set_re_resolution(INDEX, clamp_u32(value, LIMIT)); \
        }
#    define DECL_TRACKBALL_ACCESSORS(SUFFIX, INDEX)                                                                                                                                                                                  \
        DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_tb_angle_##SUFFIX, TB_GET_BODY(INDEX, TB_FIELD_ANGLE), TB_SET_BODY(INDEX, TB_FIELD_ANGLE, 359));                                                                              \
        DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_tb_drag_scroll_speed_magnification_##SUFFIX, TB_GET_BODY(INDEX, TB_FIELD_DRAG_SCROLL_SPEED_MAGNIFICATION), TB_SET_BODY(INDEX, TB_FIELD_DRAG_SCROLL_SPEED_MAGNIFICATION, 15)); \
        DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_tb_cpi_step_##SUFFIX, TB_GET_BODY(INDEX, TB_FIELD_CPI_STEP), TB_SET_BODY(INDEX, TB_FIELD_CPI_STEP, 15));                                                                      \
        DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_tb_invert_drag_scroll_x_##SUFFIX, TB_GET_BODY(INDEX, TB_FIELD_INVERT_DRAG_SCROLL_X), TB_SET_BODY(INDEX, TB_FIELD_INVERT_DRAG_SCROLL_X, 1));                                   \
        DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_tb_invert_drag_scroll_y_##SUFFIX, TB_GET_BODY(INDEX, TB_FIELD_INVERT_DRAG_SCROLL_Y), TB_SET_BODY(INDEX, TB_FIELD_INVERT_DRAG_SCROLL_Y, 1))
#    define DECL_RE_RESOLUTION_ACCESSOR(SUFFIX, INDEX) DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_re_resolution_##SUFFIX, RE_GET_BODY(INDEX), RE_SET_BODY(INDEX, 3))

DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_VALUE_ID_START, { return 0; }, {});
DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_mouse_layer_on, GENERAL_GET_BODY(GENERAL_FIELD_MOUSE_LAYER_ON), GENERAL_SET_BODY(GENERAL_FIELD_MOUSE_LAYER_ON, 1));
DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_mouse_layer_off_delay_ms, GENERAL_GET_BODY(GENERAL_FIELD_MOUSE_LAYER_OFF_DELAY_MS), GENERAL_SET_BODY(GENERAL_FIELD_MOUSE_LAYER_OFF_DELAY_MS, 63));
DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_all_keys_are_mouse_keys, GENERAL_GET_BODY(GENERAL_FIELD_ALL_KEYS_ARE_MOUSE_KEYS), GENERAL_SET_BODY(GENERAL_FIELD_ALL_KEYS_ARE_MOUSE_KEYS, 1));
DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_trackball_timeout, GENERAL_GET_BODY(GENERAL_FIELD_TRACKBALL_TIMEOUT), GENERAL_SET_BODY(GENERAL_FIELD_TRACKBALL_TIMEOUT, 3));
DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_rescan_i2c_on_read_error, GENERAL_GET_BODY(GENERAL_FIELD_RESCAN_I2C_ON_READ_ERROR), GENERAL_SET_BODY(GENERAL_FIELD_RESCAN_I2C_ON_READ_ERROR, 1));
DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_permissive_hold, GENERAL_GET_BODY(GENERAL_FIELD_PERMISSIVE_HOLD), GENERAL_SET_BODY(GENERAL_FIELD_PERMISSIVE_HOLD, 1));
DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_tapping_term_50ms, GENERAL_GET_BODY(GENERAL_FIELD_TAPPING_TERM_50MS), GENERAL_SET_BODY(GENERAL_FIELD_TAPPING_TERM_50MS, 15));
DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_display_mode, GENERAL_GET_BODY(GENERAL_FIELD_DISPLAY_MODE), GENERAL_SET_BODY(GENERAL_FIELD_DISPLAY_MODE, DisplayMode_MAX - 1));
DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_led_base_mode, GENERAL_GET_BODY(GENERAL_FIELD_LED_BASE_MODE), GENERAL_SET_BODY(GENERAL_FIELD_LED_BASE_MODE, PendantLED_MODE_BASE_MAX - 1));
DECL_TRACKBALL_ACCESSORS(1, 0);
DECL_TRACKBALL_ACCESSORS(2, 1);
DECL_TRACKBALL_ACCESSORS(3, 2);
DECL_TRACKBALL_ACCESSORS(4, 3);
DECL_RE_RESOLUTION_ACCESSOR(1, 0);
DECL_RE_RESOLUTION_ACCESSOR(2, 1);
DECL_RE_RESOLUTION_ACCESSOR(3, 2);
DECL_RE_RESOLUTION_ACCESSOR(4, 3);
DECL_RE_RESOLUTION_ACCESSOR(5, 4);
DECL_RE_RESOLUTION_ACCESSOR(6, 5);
DECL_RE_RESOLUTION_ACCESSOR(7, 6);
DECL_RE_RESOLUTION_ACCESSOR(8, 7);
DECL_RE_RESOLUTION_ACCESSOR(9, 8);
DECL_RE_RESOLUTION_ACCESSOR(10, 9);
DECL_RE_RESOLUTION_ACCESSOR(11, 10);
DECL_RE_RESOLUTION_ACCESSOR(12, 11);
DECL_RE_RESOLUTION_ACCESSOR(13, 12);
DECL_RE_RESOLUTION_ACCESSOR(14, 13);
DECL_RE_RESOLUTION_ACCESSOR(15, 14);
DECL_RE_RESOLUTION_ACCESSOR(16, 15);
DECL_VALUE_ACCESSOR(Pendant_v2_KBC_ParamID_KBC_magkey_data_bytes, GENERAL_GET_BODY(GENERAL_FIELD_MAGKEY_DATA_BYTES), GENERAL_SET_BODY(GENERAL_FIELD_MAGKEY_DATA_BYTES, MagKey_Mode_MAX - 1));

#    define FOR_EACH_KBC_ACCESSOR(X)                                       \
        X(Pendant_v2_KBC_ParamID_KBC_VALUE_ID_START)                       \
        X(Pendant_v2_KBC_ParamID_KBC_mouse_layer_on)                       \
        X(Pendant_v2_KBC_ParamID_KBC_mouse_layer_off_delay_ms)             \
        X(Pendant_v2_KBC_ParamID_KBC_all_keys_are_mouse_keys)              \
        X(Pendant_v2_KBC_ParamID_KBC_trackball_timeout)                    \
        X(Pendant_v2_KBC_ParamID_KBC_rescan_i2c_on_read_error)             \
        X(Pendant_v2_KBC_ParamID_KBC_permissive_hold)                      \
        X(Pendant_v2_KBC_ParamID_KBC_tapping_term_50ms)                    \
        X(Pendant_v2_KBC_ParamID_KBC_display_mode)                         \
        X(Pendant_v2_KBC_ParamID_KBC_led_base_mode)                        \
        X(Pendant_v2_KBC_ParamID_KBC_tb_angle_1)                           \
        X(Pendant_v2_KBC_ParamID_KBC_tb_drag_scroll_speed_magnification_1) \
        X(Pendant_v2_KBC_ParamID_KBC_tb_cpi_step_1)                        \
        X(Pendant_v2_KBC_ParamID_KBC_tb_invert_drag_scroll_x_1)            \
        X(Pendant_v2_KBC_ParamID_KBC_tb_invert_drag_scroll_y_1)            \
        X(Pendant_v2_KBC_ParamID_KBC_tb_angle_2)                           \
        X(Pendant_v2_KBC_ParamID_KBC_tb_drag_scroll_speed_magnification_2) \
        X(Pendant_v2_KBC_ParamID_KBC_tb_cpi_step_2)                        \
        X(Pendant_v2_KBC_ParamID_KBC_tb_invert_drag_scroll_x_2)            \
        X(Pendant_v2_KBC_ParamID_KBC_tb_invert_drag_scroll_y_2)            \
        X(Pendant_v2_KBC_ParamID_KBC_tb_angle_3)                           \
        X(Pendant_v2_KBC_ParamID_KBC_tb_drag_scroll_speed_magnification_3) \
        X(Pendant_v2_KBC_ParamID_KBC_tb_cpi_step_3)                        \
        X(Pendant_v2_KBC_ParamID_KBC_tb_invert_drag_scroll_x_3)            \
        X(Pendant_v2_KBC_ParamID_KBC_tb_invert_drag_scroll_y_3)            \
        X(Pendant_v2_KBC_ParamID_KBC_tb_angle_4)                           \
        X(Pendant_v2_KBC_ParamID_KBC_tb_drag_scroll_speed_magnification_4) \
        X(Pendant_v2_KBC_ParamID_KBC_tb_cpi_step_4)                        \
        X(Pendant_v2_KBC_ParamID_KBC_tb_invert_drag_scroll_x_4)            \
        X(Pendant_v2_KBC_ParamID_KBC_tb_invert_drag_scroll_y_4)            \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_1)                      \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_2)                      \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_3)                      \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_4)                      \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_5)                      \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_6)                      \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_7)                      \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_8)                      \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_9)                      \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_10)                     \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_11)                     \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_12)                     \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_13)                     \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_14)                     \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_15)                     \
        X(Pendant_v2_KBC_ParamID_KBC_re_resolution_16)                     \
        X(Pendant_v2_KBC_ParamID_KBC_magkey_data_bytes)

static const struct ValueAccessor *via_get_accessors(void) {
#    define ACCESSOR_TABLE_ENTRY(ID) accessor_##ID,
    static const struct ValueAccessor table[] = {FOR_EACH_KBC_ACCESSOR(ACCESSOR_TABLE_ENTRY)};
#    undef ACCESSOR_TABLE_ENTRY

    _Static_assert(Pendant_v2_KBC_ParamID_KBC_VALUE_ID_END == ARRAY_SIZE(table), "ViaCustomValueID: accessor table mismatch");
    return table;
}

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    Pendant_v2_Pkt_t *resp = (Pendant_v2_Pkt_t *)(void *)data;
    (void)length;

    dprintf("VCVID_via_custom_value_command_kb: cmd=%d, channel=%d, seq=%d, value_id=%d, data_length=%d\n", resp->headers.command_id, resp->headers.channel_id, resp->headers.seq, resp->headers.value_id, resp->headers.data_length);

    if (resp->headers.channel_id != id_custom_channel) {
        mark_unhandled_command(resp);
        dprintf("via_custom_value_command_kb: unhandled cmd=%u len=%u\n", resp->headers.command_id, resp->headers.data_length);
        return;
    }

    switch (resp->headers.command_id) {
        case id_custom_set_value:
            set_param(resp);
            return;
        case id_custom_get_value:
            get_param(resp);
            return;
        case id_custom_save:
            save_param();
            return;
        default:
            mark_unhandled_command(resp);
            return;
    }
}

void get_param(Pendant_v2_Pkt_t *pkt) {
    uint8_t value_id = pkt->headers.value_id;
    dprintf("[get_param] value_id=%u pkt=%p\n", value_id, (void *)pkt);

    if (handle_get_kbc_param(pkt, value_id)) {
        return;
    }

    if (Pendant_v2_HIDCommandValueID_is_known_value(value_id)) {
        dprintf("[get_param] %s v: %d\n", Pendant_v2_HIDCommandValueID_name(value_id), (int)value_id);
        if (handle_get_command(pkt, value_id)) {
            return;
        }
    }

    dprintf("[get_param] value_id %d not found\n", value_id);
}

void set_param(Pendant_v2_Pkt_t *pkt) {
    uint8_t value_id = pkt->headers.value_id;
    dprintf("[set_param] value_id=%u pkt=%p\n", value_id, (void *)pkt);

    switch (handle_set_kbc_param(pkt, value_id)) {
        case COMMAND_RESULT_HANDLED:
            apply_set_param_side_effect(value_id);
            return;
        case COMMAND_RESULT_ABORT:
            return;
        case COMMAND_RESULT_UNHANDLED:
            break;
    }

    if (!Pendant_v2_HIDCommandValueID_is_known_value(value_id)) {
        dprintf("[set_param] value_id %d not found\n", value_id);
        return;
    }

    dprintf("[set_param] %s\n", Pendant_v2_HIDCommandValueID_name(value_id));
    switch (handle_set_command(pkt, value_id)) {
        case COMMAND_RESULT_HANDLED:
            apply_set_param_side_effect(value_id);
            return;
        case COMMAND_RESULT_ABORT:
            return;
        case COMMAND_RESULT_UNHANDLED:
            dprintf("[set_param] value_id %d not handled\n", value_id);
            return;
    }
}

void save_param(void) {
    kb_config_mark_dirty_full();
}

static void apply_trackball_sensor_side_effect(uint8_t index) {
    trackball_apply_sensor_config(index);
}

static void apply_trackball_drag_scroll_side_effect(void) {
    trackball_reset_drag_scroll_accumulator();
}

#    define TRACKBALL_SENSOR_CASES(SUFFIX, INDEX)             \
        case Pendant_v2_KBC_ParamID_KBC_tb_angle_##SUFFIX:    \
        case Pendant_v2_KBC_ParamID_KBC_tb_cpi_step_##SUFFIX: \
            apply_trackball_sensor_side_effect(INDEX);        \
            break
#    define TRACKBALL_DRAG_CASES(SUFFIX)                                             \
        case Pendant_v2_KBC_ParamID_KBC_tb_drag_scroll_speed_magnification_##SUFFIX: \
        case Pendant_v2_KBC_ParamID_KBC_tb_invert_drag_scroll_x_##SUFFIX:            \
        case Pendant_v2_KBC_ParamID_KBC_tb_invert_drag_scroll_y_##SUFFIX:            \
            apply_trackball_drag_scroll_side_effect();                               \
            break

void apply_set_param_side_effect(uint8_t value_id) {
    switch (value_id) {
        case Pendant_v2_KBC_ParamID_KBC_mouse_layer_on:
            set_auto_mouse_enable(kb_config_get()->v.general.mouse_layer_on);
            break;
        case Pendant_v2_KBC_ParamID_KBC_mouse_layer_off_delay_ms:
            set_auto_mouse_timeout(calc_auto_mouse_timeout_by_kbconfig(kb_config_get()->v.general.mouse_layer_off_delay_ms));
            break;
        case Pendant_v2_KBC_ParamID_KBC_trackball_timeout:
            trackball_apply_timeout(kb_config_get()->v.general.trackball_timeout);
            break;
        case Pendant_v2_KBC_ParamID_KBC_display_mode:
            display_set_mode((DisplayMode)kb_config_get()->v.general.display_mode);
            break;
        case Pendant_v2_KBC_ParamID_KBC_led_base_mode:
            pendant_led_set_mode_base((PendantLED_MODE_BASE)kb_config_get()->v.general.led_base_mode);
            break;
            TRACKBALL_SENSOR_CASES(1, 0);
            TRACKBALL_DRAG_CASES(1);
            TRACKBALL_SENSOR_CASES(2, 1);
            TRACKBALL_DRAG_CASES(2);
            TRACKBALL_SENSOR_CASES(3, 2);
            TRACKBALL_DRAG_CASES(3);
            TRACKBALL_SENSOR_CASES(4, 3);
            TRACKBALL_DRAG_CASES(4);
        default:
            break;
    }
}

#    undef TRACKBALL_SENSOR_CASES
#    undef TRACKBALL_DRAG_CASES

static void send_multipart_chunk(Pendant_v2_MultiPartPkt_t *base, const uint8_t *payload, uint8_t idx, uint8_t chunks, uint8_t payload_size, uint16_t total_len) {
    uint16_t off  = (uint16_t)idx * payload_size;
    uint16_t rem  = (uint16_t)(total_len - off);
    uint8_t  take = (uint8_t)(rem > payload_size ? payload_size : rem);

    Pendant_v2_MultiPartPkt_t resp      = *base;
    resp.headers.data_length            = take;
    resp.additional_headers.part        = idx;
    resp.additional_headers.total_parts = chunks;
    memcpy(&resp.data[0], payload + off, take);

    uint8_t                   out[32] = {0};
    Pendant_v2_MultiPartPkt_t resp_pe;
    Pendant_v2_MultiPartPkt_copy_to_pe(&resp_pe, &resp);
    memcpy(&out[0], &resp_pe, 32);

    raw_hid_send(out, 32);
}

void send_device_list(Pendant_v2_Pkt_t *pkt) {
    static uint8_t items_buf[DEVICEITEM_BUF_SIZE];
    const size_t   payload_n = build_deviceitem_payload(items_buf, sizeof(items_buf));
    if (payload_n == 0) {
        dprintf("DeviceItem payload too large\n");
        return;
    }

    Pendant_v2_MultiPartPkt_t base = {0};
    base.headers                   = pkt->headers;

    uint16_t      total_len    = (payload_n > 0xFFFFu) ? 0xFFFFu : (uint16_t)payload_n;
    const uint8_t payload_size = sizeof(base.data);
    uint8_t       chunks       = (uint8_t)((total_len - 1) / payload_size);

    for (uint8_t idx = 0; idx < chunks + 1; idx++) {
        send_multipart_chunk(&base, items_buf, idx, chunks, payload_size, total_len);
    }
}

#    undef DECL_VALUE_ACCESSOR
#    undef GENERAL_GET_BODY
#    undef GENERAL_SET_BODY
#    undef TB_GET_BODY
#    undef TB_SET_BODY
#    undef RE_GET_BODY
#    undef RE_SET_BODY
#    undef DECL_TRACKBALL_ACCESSORS
#    undef DECL_RE_RESOLUTION_ACCESSOR
#    undef FOR_EACH_KBC_ACCESSOR

#endif
