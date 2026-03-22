#include QMK_KEYBOARD_H

#include "i2clib.h"
#include "device_scanner.h"
#include "i2c_master.h"
#include "kb_config.h"
#include "cue2keys.h"
#include "magkey_utils.h"
#include "trackball.h"
#include "oled_driver.h"
#include "display.h"
#include <stdio.h>
#include <string.h>

#ifdef OLED_ENABLE

extern DeviceList deviceList[MAX_MCP_NUM];
extern uint16_t   nDevices;

typedef enum {
    DisplayInputDeviceType_NormalKey = 0,
    DisplayInputDeviceType_MagKey,
    DisplayInputDeviceType_RotaryEncoder,
} DisplayInputDeviceType;

typedef struct {
    uint8_t                row;
    uint8_t                col;
    uint8_t                i2c_addr;
    bool                   has_i2c_addr;
    bool                   has_value;
    DisplayInputDeviceType type;
} DisplayLastInputState;

DisplayMode                display_mode                = DisplayMode_Info;
static uint8_t             display_keypress_target_row = 0;
static uint8_t             display_keypress_target_col = 0;
static DisplayLastInputState display_last_input          = {0};

uint8_t display_get_mode(void) {
    return (uint8_t)display_mode;
}

void display_set_mode(DisplayMode mode) {
    if (mode >= (uint8_t)DisplayMode_MAX) {
        mode = (uint8_t)DisplayMode_Info;
    }
    if (display_mode != (DisplayMode)mode) {
        display_mode = (DisplayMode)mode;
        oled_clear();
        // Ensure scroll mode from U1Walking doesn't keep the old screen.
        oled_scroll_off();
    }
}

void display_set_keypress_target(uint8_t row, uint8_t col) {
    dprintf("Display selected key at row=%u col=%u\n", row, col);
    display_keypress_target_row = row;
    display_keypress_target_col = col;
    display_set_mode(DisplayMode_SelectedKey);
}

bool process_record_kb_display(uint16_t keycode, keyrecord_t *record) {
    // process next oled page
    if (record->event.pressed) {
        switch (keycode) {
            case NEXT_OLED_PAGE:
                display_set_mode((uint8_t)((display_mode + 1) % DisplayMode_MAX));
                return false;
        }
    }

    return true;
}

static uint8_t display_device_key_width(Pendant_v2_ModuleType_enum_t type) {
    switch (type) {
        case Pendant_v2_ModuleType_V1_PCA9557_Keys4:
        case Pendant_v2_ModuleType_V1_XL9555_Keys4:
        case Pendant_v2_ModuleType_V2_Keys4:
        case Pendant_v2_ModuleType_V2_MagKeys4:
            return 4;
        case Pendant_v2_ModuleType_V1_PCA9557_Keys5:
        case Pendant_v2_ModuleType_V1_XL9555_Keys5:
            return 5;
        case Pendant_v2_ModuleType_V1_PCA9534A_RE:
        case Pendant_v2_ModuleType_V2_RE:
            return 1;
        default:
            return 0;
    }
}

static DisplayInputDeviceType display_device_type_from_module(Pendant_v2_ModuleType_enum_t type) {
    switch (type) {
        case Pendant_v2_ModuleType_V2_MagKeys4:
            return DisplayInputDeviceType_MagKey;
        case Pendant_v2_ModuleType_V1_PCA9534A_RE:
        case Pendant_v2_ModuleType_V2_RE:
            return DisplayInputDeviceType_RotaryEncoder;
        default:
            return DisplayInputDeviceType_NormalKey;
    }
}

static const char *display_input_type_name(DisplayInputDeviceType type) {
    switch (type) {
        case DisplayInputDeviceType_MagKey:
            return "MagKey";
        case DisplayInputDeviceType_RotaryEncoder:
            return "RotaryEnc";
        case DisplayInputDeviceType_NormalKey:
        default:
            return "NormalKey";
    }
}

static const DeviceList *display_find_device_for_key(uint8_t row, uint8_t col, DisplayInputDeviceType *out_type) {
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) {
        return NULL;
    }

    uint16_t key_index = (uint16_t)row * MATRIX_COLS + col;
    for (uint16_t i = 0; i < nDevices; i++) {
        const DeviceList *device = &deviceList[i];
        uint8_t           width  = display_device_key_width(device->type);
        if (width == 0) {
            continue;
        }

        if (key_index < device->keymapShift || key_index >= (uint16_t)(device->keymapShift + width)) {
            continue;
        }

        if (out_type) {
            *out_type = display_device_type_from_module(device->type);
        }
        return device;
    }

    return NULL;
}

static const DeviceList *display_find_encoder_device(uint8_t index) {
    uint8_t target = index;
    for (uint16_t i = 0; i < nDevices; i++) {
        const DeviceList *device = &deviceList[i];
        if (device->type != Pendant_v2_ModuleType_V1_PCA9534A_RE && device->type != Pendant_v2_ModuleType_V2_RE) {
            continue;
        }
        if (target != 0) {
            target--;
            continue;
        }
        return device;
    }
    return NULL;
}

void display_record_key_input(uint8_t row, uint8_t col) {
    const DeviceList *device = NULL;

    display_last_input.row   = row;
    display_last_input.col   = col;
    display_last_input.type  = DisplayInputDeviceType_NormalKey;
    display_last_input.has_i2c_addr = false;
    display_last_input.i2c_addr     = 0;
    display_last_input.has_value    = true;

    device = display_find_device_for_key(row, col, &display_last_input.type);
    if (device) {
        display_last_input.has_i2c_addr = true;
        display_last_input.i2c_addr     = device->address;
    }
}

void display_record_encoder_input(uint8_t index) {
    const DeviceList *device = display_find_encoder_device(index);
    if (!device) {
        return;
    }

    display_last_input.row          = (uint8_t)(device->keymapShift / MATRIX_COLS);
    display_last_input.col          = (uint8_t)(device->keymapShift % MATRIX_COLS);
    display_last_input.type         = DisplayInputDeviceType_RotaryEncoder;
    display_last_input.has_i2c_addr = true;
    display_last_input.i2c_addr     = device->address;
    display_last_input.has_value    = true;
}

static void write_padded_line(uint8_t row, const char *text) {
    char   line[22];
    size_t len = strlen(text);
    if (len > (sizeof(line) - 1)) {
        len = sizeof(line) - 1;
    }
    memset(line, ' ', sizeof(line));
    memcpy(line, text, len);
    line[21] = '\0';
    oled_set_cursor(0, row);
    oled_write(line, false);
}

static void write_value_line(uint8_t row, const char *label, uint32_t value) {
    char   value_str[11];
    char   line[22];
    size_t label_len = strlen(label);
    if (label_len > (sizeof(line) - 1)) {
        label_len = sizeof(line) - 1;
    }
    size_t value_space = (sizeof(line) - 1) - label_len;
    memset(line, ' ', sizeof(line));
    itoa(value, value_str, 10);
    size_t value_len = strlen(value_str);
    if (value_len > value_space) {
        value_len = value_space;
    }
    memcpy(line, label, label_len);
    memcpy(line + label_len, value_str, value_len);
    line[21] = '\0';
    oled_set_cursor(0, row);
    oled_write(line, false);
}

static void render_logo(void) {
    // 'U1_dot', 128x64px
    const char epd_bitmap_U1_dot[] PROGMEM = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x40, 0x20, 0x10, 0x08, 0x08, 0x04, 0x04, 0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x04, 0x04, 0x04, 0x08, 0x10, 0x20, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x18, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80, 0x80, 0x80, 0x40, 0x40, 0x40, 0x40, 0x43, 0x3c, 0x20, 0xa0, 0xa0, 0x60, 0x60, 0x20, 0x20, 0x20, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x20, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x40,
                                              0x20, 0x10, 0x10, 0x00, 0x08, 0x08, 0x00, 0x04, 0x04, 0x04, 0x3f, 0x00, 0x00, 0x00, 0x40, 0x00, 0x20, 0x10, 0x10, 0x08, 0x08, 0x04, 0x04, 0x00, 0x02, 0x82, 0x81, 0x41, 0x41, 0x20, 0x20, 0x10, 0x10, 0x08, 0x08, 0x04, 0x04, 0x02, 0x02, 0x07, 0x70, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xc0, 0x60, 0x10, 0x0c, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x08, 0x08, 0x18, 0x10, 0x30, 0x30, 0x50, 0x50, 0x90, 0x90, 0x90, 0x90, 0x10, 0x90, 0x30, 0x10, 0x08, 0x08, 0x08, 0x08, 0x04,
                                              0x04, 0x04, 0x02, 0x02, 0x01, 0xff, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x03, 0x1c, 0xe8, 0x48, 0x84, 0x04, 0x06, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x70, 0x0d, 0x01, 0x00, 0x00, 0x00, 0x60, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x40, 0x80, 0x80, 0x80, 0x40,
                                              0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x50, 0x11, 0x12, 0x14, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x1c, 0x03, 0x00, 0x00, 0x80, 0x60, 0x0c, 0x03, 0x00, 0x80, 0x80, 0xc0, 0x40, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x18, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x08, 0x10, 0x10, 0x20, 0x20, 0x41, 0xc2, 0x82, 0x81, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80, 0x40, 0xc0, 0x20, 0x00, 0x10, 0x10, 0x08, 0x08, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x04, 0x0c, 0x08, 0x10, 0x10, 0x3f, 0x20, 0x00, 0x10, 0x00, 0x08, 0x08, 0x04, 0x04, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    oled_write_raw_P(epd_bitmap_U1_dot, sizeof(epd_bitmap_U1_dot));
}

static void write_info(const char *label, uint32_t value) {
    static char value_str[11];
    itoa(value, value_str, 10);
    oled_write_P(PSTR(label), false);
    oled_write_ln(value_str, false);
}

bool oled_task_kb(void) {
    if (!oled_task_user()) {
        return false;
    }

    switch (display_mode) {
        case DisplayMode_Info: {
            // default OLED_TIMEOUT is disabled if rendering a value which changes frequently, e.g. printing scanning rate
            // so manual timeout handling is needed here
#    if OLED_TIMEOUT > 0
            if (last_input_activity_elapsed() >= OLED_TIMEOUT) {
                oled_off();
                break;
            }
#    endif

            oled_set_cursor(0, 0);
            write_info("layer: ", get_highest_layer(layer_state));
            write_info("scan/s: ", get_matrix_scan_rate());
            {
                uint32_t uptime_s = timer_read32() / 1000;
                write_info("uptime: ", uptime_s);
            }
            write_info("# Device: ", nDevices - 1);
            {
                uint8_t keynum = 0, renum = 0, tbnum = 0;
                for (uint16_t i = 0; i < nDevices; i++) {
                    switch (deviceList[i].type) {
                        case Pendant_v2_ModuleType_V1_PCA9557_Keys4:
                        case Pendant_v2_ModuleType_V1_XL9555_Keys4:
                        case Pendant_v2_ModuleType_V1_PCA9557_Keys5:
                        case Pendant_v2_ModuleType_V1_XL9555_Keys5:
                        case Pendant_v2_ModuleType_V2_Keys4:
                        case Pendant_v2_ModuleType_V2_MagKeys4:
                            keynum++;
                            break;
                        case Pendant_v2_ModuleType_V1_PCA9534A_RE:
                        case Pendant_v2_ModuleType_V2_RE:
                            renum++;
                            break;
                    }
                }
                for (uint16_t i = 0; i < trackball_get_connected_count(); i++) {
                    tbnum++;
                }
                write_info("# Keys: ", keynum);
                write_info("# RE: ", renum);
                write_info("# TB: ", tbnum);
            }
            break;
        }
        case DisplayMode_U1Walking: {
            render_logo();

            oled_scroll_set_speed(0);
            // when oled is off -> on, scrolling is ignored. so initialize scrolling again.
            {
                static bool prev_oled_on = false;
                bool        now_on       = is_oled_on();
                if (!prev_oled_on && now_on) {
                    oled_scroll_off();
                }
                prev_oled_on = now_on;
            }
            oled_scroll_right();

            break;
        }
        case DisplayMode_SelectedKey: {
            uint16_t           value       = 0;
            bool               has_value   = false;
            const kb_config_t *kb          = kb_config_get();
            uint8_t            data_bytes  = magkey_data_bytes_from_config(kb->v.general.magkey_data_bytes);
            uint8_t            bits        = magkey_bits_per_value(data_bytes);
            uint8_t            value_shift = magkey_value_shift(bits);
            uint16_t           max_value   = (bits >= 16) ? 0xFFFFu : (uint16_t)((1u << bits) - 1u);
            uint16_t           gauge_value = 0;
            uint16_t           ap_value    = 0;
            uint16_t           rp_value    = 0;
            uint16_t           ap_raw      = 0;
            uint16_t           rp_raw      = 0;
            bool               rt_enable   = false;

            if (display_keypress_target_row < MATRIX_ROWS && display_keypress_target_col < MATRIX_COLS) {
                has_value                   = magkey_get_latest_value(display_keypress_target_row, display_keypress_target_col, &value);
                const MagKeyConfigBits *cfg = &kb->v.magkey[display_keypress_target_row][display_keypress_target_col];
                ap_value                    = (uint16_t)(cfg->actualtion_point >> value_shift);
                rp_value                    = (uint16_t)(cfg->reset_point >> value_shift);
                ap_raw                      = cfg->actualtion_point;
                rp_raw                      = cfg->reset_point;
                rt_enable                   = cfg->rapid_trigger_enable;
            }
            if (value > max_value) {
                value = max_value;
            }
            gauge_value = value;

            {
                char          line[22];
                char          max_str[6];
                size_t        max_len;
                const uint8_t bar_width = 15;
                uint8_t       filled    = (max_value > 0) ? (uint8_t)(((uint32_t)gauge_value * bar_width) / max_value) : 0;

                memset(line, ' ', sizeof(line));
                line[21] = '\0';
                line[0]  = '0';
                line[1]  = '|';
                for (uint8_t i = 0; i < bar_width; i++) {
                    line[2 + i] = (i < filled) ? '#' : '-';
                }

                itoa(max_value, max_str, 10);
                max_len = strnlen(max_str, 4);
                for (size_t i = 0; i < max_len; i++) {
                    line[21 - max_len + i] = max_str[i];
                }

                oled_set_cursor(0, 0);
                oled_write(line, false);
            }

            {
                char          line[22];
                const uint8_t bar_width = 15;
                uint8_t       ap_pos    = (max_value > 0) ? (uint8_t)(((uint32_t)ap_value * bar_width) / max_value) : 0;
                uint8_t       rp_pos    = (max_value > 0) ? (uint8_t)(((uint32_t)rp_value * bar_width) / max_value) : 0;

                if (ap_pos >= bar_width) {
                    ap_pos = (uint8_t)(bar_width - 1);
                }
                if (rp_pos >= bar_width) {
                    rp_pos = (uint8_t)(bar_width - 1);
                }

                memset(line, ' ', sizeof(line));
                line[21]             = '\0';
                line[2 + ap_pos - 1] = '|';
                line[2 + rp_pos - 1] = '|';
                oled_set_cursor(0, 1);
                oled_write(line, false);

                memset(line, ' ', sizeof(line));
                line[21] = '\0';
                if (ap_pos > 0) {
                    line[1 + ap_pos] = 'A';
                    line[2 + ap_pos] = 'P';
                } else {
                    line[2 + ap_pos] = 'A';
                    line[3 + ap_pos] = 'P';
                }
                if (rp_pos > 0) {
                    line[1 + rp_pos] = 'R';
                    line[2 + rp_pos] = 'P';
                } else {
                    line[2 + rp_pos] = 'R';
                    line[3 + rp_pos] = 'P';
                }
                oled_set_cursor(0, 2);
                oled_write(line, false);
            }

            if (has_value) {
                write_value_line(4, "VAL: ", value);
            } else {
                write_padded_line(4, "VAL: N/A");
            }

            {
                char   line[22];
                char   value_str[6];
                size_t len;
                memset(line, ' ', sizeof(line));
                line[21] = '\0';
                memcpy(line, "AP:", 3);
                itoa(ap_raw, value_str, 10);
                len = strnlen(value_str, 5);
                memcpy(line + 3, value_str, len);
                memcpy(line + 9, "RP:", 3);
                itoa(rp_raw, value_str, 10);
                len = strnlen(value_str, 5);
                memcpy(line + 12, value_str, len);
                oled_set_cursor(0, 5);
                oled_write(line, false);
            }

            {
                char line[22];
                memset(line, ' ', sizeof(line));
                line[21] = '\0';
                memcpy(line, "RT:", 3);
                if (rt_enable) {
                    memcpy(line + 3, "ON", 2);
                } else {
                    memcpy(line + 3, "OFF", 3);
                }
                oled_set_cursor(0, 7);
                oled_write(line, false);
            }
            break;
        }
        case DisplayMode_InputDevice: {
            char line[22];

            write_padded_line(0, "Input Device");
            if (!display_last_input.has_value) {
                write_padded_line(2, "No input yet");
                write_padded_line(4, "ROW: -  COL: -");
                write_padded_line(6, "I2C: N/A");
                write_padded_line(7, "TYPE: -");
                break;
            }

            snprintf(line, sizeof(line), "ROW:%u COL:%u", display_last_input.row, display_last_input.col);
            write_padded_line(2, line);

            if (display_last_input.has_i2c_addr) {
                snprintf(line, sizeof(line), "I2C: 0x%02X", display_last_input.i2c_addr);
            } else {
                snprintf(line, sizeof(line), "I2C: N/A");
            }
            write_padded_line(4, line);

            snprintf(line, sizeof(line), "TYPE: %s", display_input_type_name(display_last_input.type));
            write_padded_line(6, line);
            break;
        }
        case DisplayMode_MAX:
            dprintf("Invalid Display Mode: %d\n", display_mode);
            return false;
    }

    return true;
}

static uint8_t get_oled_channel(void) {
    for (uint16_t i = 0; i < nDevices; i++) {
        if (deviceList[i].type == Pendant_v2_ModuleType_DISPLAY) {
            return deviceList[i].ch;
        }
    }
    return 0;
}

#    ifndef I2C_DATA
#        define I2C_DATA 0x40
#    endif

// Transmit/Write Funcs.
// Override for `change_channel`
bool oled_send_cmd(const uint8_t *data, uint16_t size) {
#    if defined(OLED_TRANSPORT_SPI)
    if (!spi_start(OLED_CS_PIN, false, OLED_SPI_MODE, OLED_SPI_DIVISOR)) {
        return false;
    }
    // Command Mode
    gpio_write_pin_low(OLED_DC_PIN);
    // Send the commands
    if (spi_transmit(&data[1], size - 1) != SPI_STATUS_SUCCESS) {
        spi_stop();
        return false;
    }
    spi_stop();
    return true;
#    elif defined(OLED_TRANSPORT_I2C)
    change_channel(get_oled_channel());
    i2c_status_t status = i2c_transmit((OLED_DISPLAY_ADDRESS << 1), data, size, OLED_I2C_TIMEOUT);

    return (status == I2C_STATUS_SUCCESS);
#    endif
}

__attribute__((weak)) bool oled_send_cmd_P(const uint8_t *data, uint16_t size) {
#    if defined(__AVR__)
#        if defined(OLED_TRANSPORT_SPI)
    if (!spi_start(OLED_CS_PIN, false, OLED_SPI_MODE, OLED_SPI_DIVISOR)) {
        return false;
    }
    spi_status_t status = SPI_STATUS_SUCCESS;
    // Command Mode
    gpio_write_pin_low(OLED_DC_PIN);
    // Send the commands
    for (uint16_t i = 1; i < size && status >= 0; i++) {
        status = spi_write(pgm_read_byte((const char *)&data[i]));
    }
    spi_stop();
    return (status >= 0);
#        elif defined(OLED_TRANSPORT_I2C)

    change_channel(OLED_CHANNEL);
    i2c_status_t status = i2c_transmit_P((OLED_DISPLAY_ADDRESS << 1), data, size, OLED_I2C_TIMEOUT);

    return (status == I2C_STATUS_SUCCESS);
#        endif
#    else
    change_channel(get_oled_channel());
    return oled_send_cmd(data, size);
#    endif
}

__attribute__((weak)) bool oled_send_data(const uint8_t *data, uint16_t size) {
#    if defined(OLED_TRANSPORT_SPI)
    if (!spi_start(OLED_CS_PIN, false, OLED_SPI_MODE, OLED_SPI_DIVISOR)) {
        return false;
    }
    // Data Mode
    gpio_write_pin_high(OLED_DC_PIN);
    // Send the commands
    if (spi_transmit(data, size) != SPI_STATUS_SUCCESS) {
        spi_stop();
        return false;
    }
    spi_stop();
    return true;
#    elif defined(OLED_TRANSPORT_I2C)
    change_channel(get_oled_channel());
    i2c_status_t status = i2c_write_register((OLED_DISPLAY_ADDRESS << 1), I2C_DATA, data, size, OLED_I2C_TIMEOUT);
    return (status == I2C_STATUS_SUCCESS);
#    endif
}

#endif
