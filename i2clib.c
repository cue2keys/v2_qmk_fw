#include QMK_KEYBOARD_H
#include "i2clib.h"
#include <stdint.h>
#include "i2c_master.h"
#include "debug.h"
#include "wait.h"
#include "module_reader.h"
#include "pendant_reader.h"

static Keys_Data unknown_keys_data(void) {
    Keys_Data data = {.type = Pendant_v2_ModuleType_UNKNOWN};
    return data;
}

static bool write_register_1byte_with_log(uint8_t address, uint8_t ctrl_addr, uint8_t data, const char *device_name) {
    i2c_status_t expander_status = i2c_write_register(address << 1, ctrl_addr, &data, 1, MY_I2C_TIMEOUT);
    if (expander_status == I2C_STATUS_SUCCESS) {
        dprintf("[%s set register] %X, reg:%X, data:%X\n", device_name, address, ctrl_addr, data);
        return true;
    }

    dprintf("[%s set register] failed to set input %d! %X, reg:%X, data:%X\n", device_name, (int)expander_status, address, ctrl_addr, data);
    return false;
}

static bool read_register_bytes(uint8_t address, uint8_t register_address, uint8_t *data, uint8_t data_length) {
    i2c_status_t expander_status = i2c_read_register(address << 1, register_address, data, data_length, MY_I2C_TIMEOUT);
    if (expander_status != I2C_STATUS_SUCCESS) {
        dprintf("failed to read data from %X! %X\n", address, expander_status);
        return false;
    }

    return true;
}

void i2c_clean_and_init(void) {
    // Try releasing special pins for a short time
    palSetLineMode(I2C1_SCL_PIN, PAL_MODE_INPUT);
    palSetLineMode(I2C1_SDA_PIN, PAL_MODE_INPUT);

    chThdSleepMilliseconds(10);

    // additionally, clean i2c bus to avoid invalid status forever
    palSetLineMode(I2C1_SCL_PIN, PAL_MODE_OUTPUT_PUSHPULL);
    palWriteLine(I2C1_SCL_PIN, PAL_HIGH);

    chThdSleepMilliseconds(30);
#if defined(USE_GPIOV1)
    palSetLineMode(I2C1_SCL_PIN, I2C1_SCL_PAL_MODE);
    palSetLineMode(I2C1_SDA_PIN, I2C1_SDA_PAL_MODE);
#else
    palSetLineMode(I2C1_SCL_PIN, PAL_MODE_ALTERNATE(I2C1_SCL_PAL_MODE) | PAL_OUTPUT_TYPE_OPENDRAIN);
    palSetLineMode(I2C1_SDA_PIN, PAL_MODE_ALTERNATE(I2C1_SDA_PAL_MODE) | PAL_OUTPUT_TYPE_OPENDRAIN);
#endif
}

void i2c_init(void) {
    static bool is_initialised = false;
    if (!is_initialised) {
        is_initialised = true;
        i2c_clean_and_init();
    }
}

bool is_v2_module_address(uint8_t address) {
    bool valid = (Module_v2_Consts_MIN_DYNAMIC_ADDRESS <= address && address <= Module_v2_Consts_MAX_DYNAMIC_ADDRESS);
    valid &= !(PCA9557_FROM_ADDR <= address && address < PCA9557_END_ADDR);
    valid &= !(XL9555_FROM_ADDR <= address && address < XL9555_END_ADDR);
    valid &= !(PCA9534A_FROM_ADDR <= address && address < PCA9534A_END_ADDR);
    valid &= !(I2C_MULTIPX_ADDR_IGNORE_START <= address && address <= I2C_MULTIPX_ADDR_IGNORE_END);
    valid &= (address != I2C_MULTIPX_ADDR);
    valid &= (address != I2C_OLED_DISPLAY_ADDR);
    return valid;
}

// PCA9546A
#define LAST_CHANNEL_DEFAULT 0xFF
static uint8_t      last_channel               = LAST_CHANNEL_DEFAULT;
static bool         change_channel_failed      = false;
static i2c_status_t change_channel_last_status = I2C_STATUS_SUCCESS;
bool                change_channel(uint8_t channel) {
    // channel must be 0-4
    if (channel < 0 || 4 < channel) {
        dprintf("Invalid channel %u!\n", channel);
        return false;
    }
    uint8_t data = 0x01 << (channel - 1);
    if (channel == 0) {
        // disable all devices under MUX
        data = 0x00;
    }

    // if the channel is the same as last time, do not send command again to increase performance
    if (last_channel == channel) {
        change_channel_failed      = false;
        change_channel_last_status = I2C_STATUS_SUCCESS;
        return true;
    }

    // PCA9546A expects a single control byte write (no register address).
    i2c_status_t expander_status = i2c_transmit(I2C_MULTIPX_ADDR << 1, &data, 1, MY_I2C_TIMEOUT);
    if (expander_status == I2C_STATUS_SUCCESS) {
        wait_us(1); // Allow the bus to settle after switching.
        last_channel               = channel;
        change_channel_failed      = false;
        change_channel_last_status = I2C_STATUS_SUCCESS;
        return true;
    } else {
        change_channel_failed      = true;
        change_channel_last_status = expander_status;
        change_channel_reset_last_read();
        return false;
    }
}

void change_channel_reset_last_read(void) {
    last_channel = LAST_CHANNEL_DEFAULT;
}

bool change_channel_get_last_failure(i2c_status_t *status_out) {
    if (!change_channel_failed) {
        return false;
    }
    if (status_out != NULL) {
        *status_out = change_channel_last_status;
    }
    return true;
}

bool write_PCA9557_register_1byte(uint8_t address, uint8_t ctrl_addr, uint8_t data) {
    return write_register_1byte_with_log(address, ctrl_addr, data, "PCA9557");
}

Keys_Data read_PCA9557_register(uint8_t address) {
    uint8_t inputData = 0;
    if (!read_register_bytes(address, 0x0, &inputData, 1)) {
        return unknown_keys_data();
    }

    // upper 3 bits are type indication
    uint8_t type_data = (inputData & (0x07 << 5)) >> 5;

    Pendant_v2_ModuleType_enum_t type = Pendant_v2_ModuleType_UNKNOWN;
    if (type_data == 2) {
        type = Pendant_v2_ModuleType_V1_PCA9557_Keys5;
    } else if (type_data == 1) {
        type = Pendant_v2_ModuleType_V1_PCA9557_Keys4;
    } else {
        // unknown type
        dprintf("failed to read type from %X! Type Unknown: %X, type_data: %X\n", address, type, type_data);
        return unknown_keys_data();
    }
    // lower 5 bits are key input data. They are pulled-up, so they need to be inverted
    uint8_t data = (~inputData & 0x1F);

    if (type == Pendant_v2_ModuleType_V1_PCA9557_Keys4) {
        // remove 1 bit if the device is `4 keys`
        data &= 0x0F;
    } else if (type == Pendant_v2_ModuleType_V1_PCA9557_Keys5) {
        // 5 keys
        // do nothing
    } else {
        // unknown
        return unknown_keys_data();
    }

    Keys_Data d = {.type = type};
    memcpy(d.data, &data, 1);

    return d;
}

bool write_XL9555_register_1byte(uint8_t address, uint8_t ctrl_addr, uint8_t data) {
    return write_register_1byte_with_log(address, ctrl_addr, data, "XL9555");
}

Keys_Data read_XL9555_register(uint8_t address) {
    uint8_t inputData[2] = {0x11, 0x22};
    if (!read_register_bytes(address, 0x0, inputData, 2)) {
        return unknown_keys_data();
    }

    // upper 4 bits are type indication
    // Note: temporary, this device
    uint8_t type_data = (inputData[0] & 0xF0) >> 4;

    Pendant_v2_ModuleType_enum_t type = Pendant_v2_ModuleType_UNKNOWN;
    if (type_data == 0x2) {
        type = Pendant_v2_ModuleType_V1_XL9555_Keys5;
    } else if (type_data == 0x0 || type_data == 0xF) { // temporary
        type = Pendant_v2_ModuleType_V1_XL9555_Keys4;
    } else {
        // unknown type
        dprintf("failed to read data from %X! Type Unknown: %X, type_data: %X\n", address, type, type_data);
        return unknown_keys_data();
    }
    // key input data are on 2 bytes
    // inputData[0]: lower 4 bits
    // inputData[1]: lower 1 bits
    // They are pulled-up, so they need to be inverted
    uint8_t data = (uint8_t)((inputData[1] << 4) | (0x0F & inputData[0]));
    if (type == Pendant_v2_ModuleType_V1_XL9555_Keys4) {
        // remove 1 bit if the device is `4 keys`
        data &= 0x0F;
        data ^= 0x0F;
    } else if (type == Pendant_v2_ModuleType_V1_XL9555_Keys5) {
        data ^= 0x1F;
    }

    Keys_Data d = {.type = type};
    memcpy(d.data, &data, 1);

    return d;
}

bool write_PCA9534A_register_1byte(uint8_t address, uint8_t ctrl_addr, uint8_t data) {
    return write_register_1byte_with_log(address, ctrl_addr, data, "PCA9534A");
}

Keys_Data read_PCA9534A_register(uint8_t address) {
    uint8_t inputData = 0;
    if (!read_register_bytes(address, 0x0, &inputData, 1)) {
        return unknown_keys_data();
    }

    // upper 3 bits are type indication
    uint8_t type_data = (inputData & (0xFF << 5)) >> 5;

    Pendant_v2_ModuleType_enum_t type = Pendant_v2_ModuleType_UNKNOWN;
    if (type_data == 2) {
        type = Pendant_v2_ModuleType_V1_PCA9534A_RE;
    } else {
        // unknown type
        dprintf("failed to read type from %X! Type Unknown: %X, type_data: %X, inputData: %X \n", address, type, type_data, inputData);
        return unknown_keys_data();
    }
    // lower 5 bits are input data
    uint8_t data = (inputData & 0x1F);

    if (type == Pendant_v2_ModuleType_V1_PCA9534A_RE) {
        // use only lower 3 bits. RO_A, RO_B, L_E
        data &= 0x07;
    } else {
        // unknown
        return unknown_keys_data();
    }

    Keys_Data d = {.type = type};
    memcpy(d.data, &data, 1);

    return d;
}

Keys_Data init_PCA9557(uint8_t address) {
    bool success = true;

    // pins: input
    success &= write_PCA9557_register_1byte(address, 0x3, 0xFF);
    // polarity: no change
    success &= write_PCA9557_register_1byte(address, 0x2, 0x00);

    if (!success) {
        dprintf("Failed to init PCA9557: %X\n", address);
        return unknown_keys_data();
    }

    // load inputs to detect the type
    Keys_Data data = read_PCA9557_register(address);
    return data;
}

Keys_Data init_XL9555(uint8_t address) {
    bool success = true;

    // pins: input
    success &= write_XL9555_register_1byte(address, 0x6, 0xFF);
    success &= write_XL9555_register_1byte(address, 0x7, 0xFF);
    // polarity: no change
    success &= write_XL9555_register_1byte(address, 0x4, 0x00);
    success &= write_XL9555_register_1byte(address, 0x5, 0x00);

    if (!success) {
        dprintf("Failed to init XL9555: %X\n", address);
        return unknown_keys_data();
    }

    // load inputs to detect the type
    Keys_Data data = read_XL9555_register(address);
    return data;
}

Keys_Data init_PCA9534A(uint8_t address) {
    bool success = true;

    // pins: input
    success &= write_PCA9534A_register_1byte(address, 0x3, 0xFF);
    // polarity: no change
    success &= write_PCA9534A_register_1byte(address, 0x2, 0x00);

    if (!success) {
        dprintf("Failed to init PCA9534A: %X\n", address);
        return unknown_keys_data();
    }

    // load inputs to detect the type
    Keys_Data data = read_PCA9534A_register(address);
    return data;
}

Keys_Data init_v2_module(uint8_t address) {
    uint8_t inputData = 0;
    uint8_t status    = i2c_read_register(address << 1, Module_v2_Register_GET_TYPE, &inputData, 1, MY_I2C_TIMEOUT);

    if (status != I2C_STATUS_SUCCESS) {
        dprintf("Failed to init v2 module: %X, %d\n", address, status);
        return unknown_keys_data();
    }

    // detect module type
    Keys_Data data = {.type = Pendant_v2_ModuleType_UNKNOWN};
    if (inputData == Module_v2_Type_KEY4) {
        data.type = Pendant_v2_ModuleType_V2_Keys4;
    } else if (inputData == Module_v2_Type_RE) {
        data.type = Pendant_v2_ModuleType_V2_RE;
    } else if (inputData == Module_v2_Type_MAGKEY4) {
        data.type = Pendant_v2_ModuleType_V2_MagKeys4;
    } else {
        dprintf("Unknown v2 module type: %X, inputData: %X\n", address, inputData);
    }
    return data;
}

uint64_t read_v2_uid(uint8_t address) {
    uint8_t uidData[8] = {0};
    uint8_t status     = i2c_read_register(address << 1, Module_v2_Register_GET_UID, uidData, 8, MY_I2C_TIMEOUT);

    if (status != I2C_STATUS_SUCCESS) {
        dprintf("Failed to get v2 module UID: %X, %d\n", address, status);
        return 0;
    }

    uint64_t uid = 0;
    for (uint8_t i = 0; i < 8; i++) {
        uid |= ((uint64_t)uidData[i]) << (i * 8);
    }

    return uid;
}

bool update_v2_address_write(uint8_t channel, uint8_t old_address, uint8_t new_address) {
    bool success = change_channel(channel);
    if (!success) {
        dprintf("Failed to change channel to %d to update v2 module address\n", channel);
        return false;
    }
    uint8_t data[1] = {new_address};
    uint8_t status  = i2c_write_register(old_address << 1, Module_v2_Register_CHANGE_ADDR, data, 1, MY_I2C_TIMEOUT);

    if (status != I2C_STATUS_SUCCESS) {
        dprintf("Failed to change v2 module address: %X to %X, %d\n", old_address, new_address, status);
        return false;
    }

    dprintf("Changed v2 module address: %X to %X\n", old_address, new_address);

    return true;
}

Keys_Data read_v2_key4_register(uint8_t address) {
    uint8_t inputData = 0;
    if (!read_register_bytes(address, Module_v2_Register_GET_DATA, &inputData, 1)) {
        return unknown_keys_data();
    }

    // key input data are on 1 bytes
    // inputData[0]: lower 4 bits, 0000(K4)(K3)(K2)(K1)
    // They are pulled-down
    Keys_Data d = {.type = Pendant_v2_ModuleType_V2_Keys4, .data = {inputData & 0x0F}};
    return d;
}

bool read_v2_magkey4_register(uint8_t address, uint8_t regaddr, uint8_t length, uint8_t *out_data) {
    if (length > 6) {
        dprintf("Invalid length to read magkey4: %u\n", length);
        return false;
    }
    uint8_t first_read[6] = {0};
    if (!read_register_bytes(address, regaddr, first_read, length)) {
        return false;
    }

    for (uint8_t i = 0; i < length; i++) {
        out_data[i] = first_read[i];
    }
    return true;
}

Keys_Data read_v2_re_register(uint8_t address) {
    uint8_t inputData = 0;
    if (!read_register_bytes(address, Module_v2_Register_GET_DATA, &inputData, 1)) {
        return unknown_keys_data();
    }

    // lower 3 bits are input data
    uint8_t   data = (inputData & 0x07);
    Keys_Data d    = {.type = Pendant_v2_ModuleType_V2_RE, .data = {data}};
    return d;
}
