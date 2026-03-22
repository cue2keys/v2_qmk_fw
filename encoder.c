#include QMK_KEYBOARD_H
#include "i2clib.h"
#include "device_scanner.h"
#include "kb_config.h"
#include "display.h"
#include "./drivers/encoder_dynamic_res.h"

extern DeviceList deviceList[MAX_MCP_NUM];
extern uint16_t   nDevices;

#ifdef ENCODER_ENABLE

uint8_t encoder_data[NUM_ENCODERS] = {0};

void keyboard_post_init_kb_encoder(const kb_config_t *kb_cfg) {
    for (uint8_t i = 0; i < NUM_ENCODERS; i++) {
        dynamic_res_encoder_update_res(i, kb_cfg->v.re[i].resolution);
    }
}

uint8_t encoder_quadrature_read_pin(uint8_t index, bool pad_b) {
    uint8_t target = index;
    for (uint16_t i = 0; i < nDevices; i++) {
        if (deviceList[i].type == Pendant_v2_ModuleType_V1_PCA9534A_RE || deviceList[i].type == Pendant_v2_ModuleType_V2_RE) {
            // skip if this RE is not the indexed one
            if (target != 0) {
                target--;
                continue;
            }

            const uint8_t data = encoder_data[index];

            // data -> XXXXX (click) (B) (A)
            if (pad_b) {
                return (data & (0x01 << 1)) >> 1;
            } else {
                return (data & (0x01 << 0)) >> 0;
            }
        }
    }

    // if reaches
    return 0;
}

bool encoder_update_kb(uint8_t index, bool clockwise) {
#ifdef OLED_ENABLE
    display_record_encoder_input(index);
#endif
    return encoder_update_user(index, clockwise);
}

// override existing weak function
void encoder_driver_task(void) {
    for (uint8_t i = 0; i < NUM_ENCODERS; i++) {
        dynamic_res_encoder_quadrature_handle_read(i, encoder_quadrature_read_pin(i, false), encoder_quadrature_read_pin(i, true));
    }
}
#endif
