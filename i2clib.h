#pragma once

#include <stdint.h>
#include <stdio.h>
#include "keycodes.h"
#include "debug.h"
#include "i2c_master.h"
#include "pendant_reader.h"

// Address here is 7-bit, do not shift left

// 0x18 is the beginning of PCA9557
#define PCA9557_FROM_ADDR 0x18
#define PCA9557_END_ADDR (PCA9557_FROM_ADDR + 8)

// 0x20 is the beginning of XL9555/TCA9535
#define XL9555_FROM_ADDR 0x20
#define XL9555_END_ADDR (XL9555_FROM_ADDR + 8)

// 0x38 is the beginning of PCA9534A
// NOTICE: OLED resides on 0x3C, thus MAX_RE_PER_CH_NUM is 4 to skip this address.
// Additionally, on the hardware side, the A2 pin needs to be low.
#define PCA9534A_FROM_ADDR 0x38
#define PCA9534A_END_ADDR (PCA9534A_FROM_ADDR + 4)

// PCA9546A I2C Multiplexer
#define I2C_MULTIPX_ADDR 0x70
// I'm not sure the reason, but 0x71- are ignored if it's under the PCA9546A channel.
// so do not use these addresses.
#define I2C_MULTIPX_ADDR_IGNORE_START 0x71
#define I2C_MULTIPX_ADDR_IGNORE_END 0x80

#define I2C_OLED_DISPLAY_ADDR 0x3C

#define MY_I2C_TIMEOUT 1

#define MAX_MEX_CH 4

// # of modules limit
#define MAX_MCP_NUM 60

// # of System Key
#define SYSTEM_KEY_NUM 8

// maximum number of keymaps per channel (alligned)
#define MAX_KEYMAP_PER_CH_NUM 64
// maximum number of encoder (alligned)
#define MAX_ENCODER_KEYMAP_NUM 32
#define ENCODER_KEYMAP_START_IDX (MAX_KEYMAP_PER_CH_NUM * MAX_MEX_CH)
// 4 channels + encoder rows
#define SYSTEM_KEY_START_IDX (ENCODER_KEYMAP_START_IDX + MAX_ENCODER_KEYMAP_NUM)

typedef struct {
    Pendant_v2_ModuleType_enum_t type;
    uint8_t                      data[2]; // key input, joysticks, etc
} Keys_Data;

void i2c_clean_and_init(void);

bool is_v2_module_address(uint8_t address);
bool change_channel(uint8_t channel);
void change_channel_reset_last_read(void);
bool change_channel_get_last_failure(i2c_status_t *status_out);

Keys_Data init_PCA9557(uint8_t address);
Keys_Data read_PCA9557_register(uint8_t address);
Keys_Data init_XL9555(uint8_t address);
Keys_Data read_XL9555_register(uint8_t address);
Keys_Data init_PCA9534A(uint8_t address);
Keys_Data read_PCA9534A_register(uint8_t address);

Keys_Data init_v2_module(uint8_t address);
uint64_t  read_v2_uid(uint8_t address);
bool      update_v2_address_write(uint8_t channel, uint8_t old_address, uint8_t new_address);
Keys_Data read_v2_key4_register(uint8_t address);
bool      read_v2_magkey4_register(uint8_t address, uint8_t regaddr, uint8_t length, uint8_t *out_data);
Keys_Data read_v2_re_register(uint8_t address);
