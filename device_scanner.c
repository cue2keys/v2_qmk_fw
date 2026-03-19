
#include "i2clib.h"
#include "i2c_master.h"
#include "device_scanner.h"
#include "debug.h"
#include "pendant_led.h"
#include "wait.h"
#include "module_reader.h"
#include "pendant_reader.h"
#include "magkey_utils.h"
#include <stdint.h>

DeviceList deviceList[MAX_MCP_NUM];
uint16_t   nDevices = 0;

static IdentifiedDevice identify_devices(uint8_t address);

static void append_device(uint8_t ch, uint8_t address, const IdentifiedDevice *device) {
    DeviceList dev = {
        .ch                 = ch,
        .address            = address,
        .type               = device->type,
        .keymapShift        = 0,
        .UID                = device->UID,
        .consecutive_errors = 0,
    };
    deviceList[nDevices] = dev;
    nDevices++;
}

// uid to 16 characters string
void uid_to_string(uint64_t uid, char *buffer, size_t buffer_size) {
    if (buffer_size < 17) {
        dprintf("not enough space to convert UID, %zu bytes given\n", buffer_size);
        buffer[0] = '\0';
        return;
    }
    for (int i = 0; i < 64 / 8; i++) {
        uint8_t data = (uid >> (i * 8)) & 0xFF;
        sprintf(&buffer[i * 2], "%02X", data);
    }
    buffer[16] = '\0';
}

typedef enum {
    ShiftType_Key_Start = 0,
    ShiftType_RE_Start  = 1,
} ShiftType;

uint16_t get_keymap_start(uint8_t ch, ShiftType type) {
    uint16_t reShift = ENCODER_KEYMAP_START_IDX;
    uint16_t chShift = MAX_KEYMAP_PER_CH_NUM;

    if (type == ShiftType_Key_Start) {
        return chShift * (ch - 1);
    } else if (type == ShiftType_RE_Start) {
        return reShift;
    } else {
        dprintf("Invalid Shift Type: %d", type);
        return 0;
    }
}

// Treat MATRIX_COLS as the row alignment and place the next module so it does not cross a row boundary.
// The previous module position is given as keymapShift; return the position advanced by shiftValue.
uint16_t get_aligned_keymap_shift(uint16_t keymapShift, uint16_t shiftValue) {
    uint16_t col = keymapShift % MATRIX_COLS;
    if ((uint16_t)(col + shiftValue) > MATRIX_COLS) {
        return keymapShift + (MATRIX_COLS - col);
    }

    return keymapShift;
}

static uint8_t device_type_sort_rank(Pendant_v2_ModuleType_enum_t type) {
    switch (type) {
        case Pendant_v2_ModuleType_V2_Keys4:
        case Pendant_v2_ModuleType_V2_MagKeys4:
            return 0;
        case Pendant_v2_ModuleType_V2_RE:
            return 1;
        case Pendant_v2_ModuleType_V1_PCA9557_Keys4:
        case Pendant_v2_ModuleType_V1_PCA9557_Keys5:
        case Pendant_v2_ModuleType_V1_XL9555_Keys4:
        case Pendant_v2_ModuleType_V1_XL9555_Keys5:
            return 2;
        case Pendant_v2_ModuleType_V1_PCA9534A_RE:
            return 3;
        default:
            return 100;
    }
}

static bool deviceListOrderCompare(Pendant_v2_ModuleType_enum_t a, Pendant_v2_ModuleType_enum_t b) {
    uint8_t rank_a = device_type_sort_rank(a);
    uint8_t rank_b = device_type_sort_rank(b);
    if (rank_a != rank_b) {
        return rank_a > rank_b;
    }
    return false;
}

static bool deviceListShouldSwap(const DeviceList *a, const DeviceList *b) {
    if (a->ch != b->ch) {
        return a->ch > b->ch;
    }
    if (deviceListOrderCompare(a->type, b->type)) {
        return true;
    }
    // sort in the same type
    if (a->address > b->address) {
        return true;
    }
    return false;
}

static bool channel_zero_has_address(uint8_t address) {
    for (uint16_t i = 0; i < nDevices; i++) {
        if (deviceList[i].ch == 0 && deviceList[i].address == address) {
            return true;
        }
    }
    return false;
}

static void scan_channel_zero_devices(void) {
    change_channel(0);
    for (uint8_t addr = 0; addr < 0x7F; addr++) {
        i2c_status_t error = i2c_ping_address(addr << 1, MY_I2C_TIMEOUT);
        if (error != I2C_STATUS_SUCCESS) {
            continue;
        }

        IdentifiedDevice device = identify_devices(addr);
        if (device.type == Pendant_v2_ModuleType_UNKNOWN) {
            continue;
        }

        dprintf(" Channel 0 device found at address 0x%02X\n", addr);
        append_device(0, addr, &device);
    }
}

static void scan_mux_channel(uint8_t ch) {
    change_channel(ch);

    for (uint8_t addr = 0; addr < 0x7F; addr++) {
        i2c_status_t error = i2c_ping_address(addr << 1, MY_I2C_TIMEOUT);
        if (error != I2C_STATUS_SUCCESS) {
            continue;
        }

        dprintf(" Channel %d device found at address 0x%02X\n", ch, addr);
        if (channel_zero_has_address(addr)) {
            dprintf(" Skipping module at ch %X address 0x%02X since it exists on channel 0\n", ch, addr);
            continue;
        }

        IdentifiedDevice device = identify_devices(addr);
        if (device.type == Pendant_v2_ModuleType_UNKNOWN) {
            continue;
        }

        append_device(ch, addr, &device);
    }
}

static void sort_device_list(void) {
    for (uint16_t i = 0; i < nDevices - 1; i++) {
        for (uint16_t j = i + 1; j < nDevices; j++) {
            if (deviceListShouldSwap(&deviceList[i], &deviceList[j])) {
                DeviceList temp = deviceList[i];
                deviceList[i]   = deviceList[j];
                deviceList[j]   = temp;
            }
        }
    }
}

static uint8_t key_device_width(Pendant_v2_ModuleType_enum_t type) {
    switch (type) {
        case Pendant_v2_ModuleType_V2_Keys4:
        case Pendant_v2_ModuleType_V2_MagKeys4:
        case Pendant_v2_ModuleType_V1_PCA9557_Keys4:
        case Pendant_v2_ModuleType_V1_XL9555_Keys4:
            return 4;
        case Pendant_v2_ModuleType_V1_PCA9557_Keys5:
        case Pendant_v2_ModuleType_V1_XL9555_Keys5:
            return 5;
        default:
            return 0;
    }
}

static void assign_device_keymap_shift(DeviceList *device, uint16_t *next_keymap_shift, uint16_t *next_re_keymap_shift) {
    uint8_t shift_value = key_device_width(device->type);
    if (shift_value > 0) {
        *next_keymap_shift  = get_aligned_keymap_shift(*next_keymap_shift, shift_value);
        device->keymapShift = *next_keymap_shift;
        *next_keymap_shift += shift_value;
        return;
    }

    if (device->type == Pendant_v2_ModuleType_V2_RE) {
        *next_re_keymap_shift = get_aligned_keymap_shift(*next_re_keymap_shift, 1);
        device->keymapShift   = *next_re_keymap_shift;
        *next_re_keymap_shift += 1;
    }
}

static void assign_keymap_shifts(void) {
    uint16_t nextREKeymapShift = get_keymap_start(0, ShiftType_RE_Start);
    uint16_t nextKeymapShift   = 0;
    int8_t   prev_ch           = -1;

    for (uint16_t i = 0; i < nDevices; i++) {
        if (deviceList[i].ch == 0) {
            continue;
        }
        if (prev_ch != deviceList[i].ch) {
            nextKeymapShift = get_keymap_start(deviceList[i].ch, ShiftType_Key_Start);
            prev_ch         = deviceList[i].ch;
        }

        assign_device_keymap_shift(&deviceList[i], &nextKeymapShift, &nextREKeymapShift);
    }
}

// just identify device at the given address and channel
static IdentifiedDevice identify_devices(uint8_t address) {
    if (is_v2_module_address(address)) {
        Keys_Data data = init_v2_module(address);
        if (data.type != Pendant_v2_ModuleType_UNKNOWN) {
            uint64_t uid = read_v2_uid(address);
            char     uid_str[17];
            uid_to_string(uid, uid_str, sizeof(uid_str));
            dprintf("V2 Module found at address 0x%02X, type: %d, UID: %s\n", address, data.type, uid_str);
            return (IdentifiedDevice){.address = address, .type = data.type, .UID = uid};
        } else {
            dprintf("V2 Module init failed at address 0x%02X, type: %d\n", address, data.type);
            return (IdentifiedDevice){.address = address, .type = Pendant_v2_ModuleType_UNKNOWN, .UID = 0};
        }
    } else if (address == I2C_MULTIPX_ADDR) {
        dprintf("I2C MUX found at address: 0x%02X\n", address);
        return (IdentifiedDevice){.address = address, .type = Pendant_v2_ModuleType_I2C_MUX, .UID = 0};
    } else if (address == I2C_OLED_DISPLAY_ADDR) {
        dprintf("I2C Display found at address: 0x%02X\n", address);
        return (IdentifiedDevice){.address = address, .type = Pendant_v2_ModuleType_DISPLAY, .UID = 0};
    } else if (PCA9557_FROM_ADDR <= address && address < PCA9557_END_ADDR) {
        Keys_Data data = init_PCA9557(address);
        if (data.type == Pendant_v2_ModuleType_UNKNOWN) {
            dprintf("[PCA9557_init] Failed: 0x%X, type: %d \n", address, data.type);
        } else {
            dprintf("PCA9557 found at address 0x%02X, type: %d\n", address, data.type);
            return (IdentifiedDevice){.address = address, .type = data.type, .UID = 0};
        }
    } else if (XL9555_FROM_ADDR <= address && address < XL9555_END_ADDR) {
        Keys_Data data = init_XL9555(address);
        if (data.type == Pendant_v2_ModuleType_UNKNOWN) {
            dprintf("[XL9555_init] Failed: 0x%X, type: %d \n", address, data.type);
        } else {
            dprintf("XL9555 found at address 0x%02X, type: %d\n", address, data.type);
            return (IdentifiedDevice){.address = address, .type = data.type, .UID = 0};
        }
    } else if (PCA9534A_FROM_ADDR <= address && address < PCA9534A_END_ADDR) {
        Keys_Data data = init_PCA9534A(address);
        if (data.type == Pendant_v2_ModuleType_UNKNOWN) {
            dprintf("[PCA9534A_init] Failed: 0x%X, type: %d \n", address, data.type);
        } else {
            dprintf("PCA9534A found at address 0x%02X, type: %d\n", address, data.type);
            return (IdentifiedDevice){.address = address, .type = data.type, .UID = 0};
        }
    } else {
        dprintf("Not a module address: 0x%02X\n", address);
    }

    // fallback
    return (IdentifiedDevice){.address = address, .type = Pendant_v2_ModuleType_UNKNOWN, .UID = 0};
}

Pendant_v2_SetI2CAddrCmdErrorCode_enum_t update_v2_address(uint8_t channel, uint8_t old_address, uint8_t new_address) {
    if (old_address == new_address) {
        dprintf("v2 I2C address unchanged: ch %u addr 0x%02X\n", channel, old_address);
        return Pendant_v2_SetI2CAddrCmdErrorCode_ADDRESS_UNCHANGED;
    }

    if (!is_v2_module_address(new_address)) {
        dprintf("Invalid new v2 I2C address update: ch %u old 0x%02X new 0x%02X\n", channel, old_address, new_address);
        return Pendant_v2_SetI2CAddrCmdErrorCode_INVALID_NEW_ADDRESS;
    }

    for (uint16_t i = 0; i < nDevices; i++) {
        if (deviceList[i].ch == channel && deviceList[i].address == new_address) {
            dprintf("v2 I2C address already in use: ch %u addr 0x%02X\n", channel, new_address);

            return Pendant_v2_SetI2CAddrCmdErrorCode_ALREADY_IN_USE;
        }
    }

    if (!update_v2_address_write(channel, old_address, new_address)) {
        return Pendant_v2_SetI2CAddrCmdErrorCode_WRITE_FAILED;
    }

    // rescan is required
    do_scan();

    return Pendant_v2_SetI2CAddrCmdErrorCode_SUCCESS;
}

void do_scan(void) {
    // before doing scan, clean bus state and re-initialize pins
    i2c_clean_and_init();

    nDevices = 0;
    magkey_reset_cached_values();

    dprintf("Scanning...\n");

    // firstly, access to the I2C multiplexer
    {
        i2c_status_t error = i2c_ping_address(I2C_MULTIPX_ADDR << 1, MY_I2C_TIMEOUT);
        if (error == I2C_STATUS_SUCCESS) {
            dprintf(" MUX found at address 0x%02X\n", I2C_MULTIPX_ADDR);
        } else {
            dprintf(" MUX error (%d) at address 0x%02X\n", (int)error, I2C_MULTIPX_ADDR);
            return;
        }
    }

    scan_channel_zero_devices();

    for (uint8_t ch = 1; ch <= MAX_MEX_CH; ch++) {
        scan_mux_channel(ch);
    }

    // sort deviceList by channel and type. channel 0 to 4, v2 first, then v1, then unknown; same type by address.
    sort_device_list();

    // set keymapshift
    assign_keymap_shifts();

    {
        dprintf("#devices: %d\n", nDevices);
        for (uint16_t i = 0; i < nDevices; i++) {
            dprintf("[I2C] %d, 0x%X %s, shift: %d\n", deviceList[i].ch, deviceList[i].address, Pendant_v2_ModuleType_name(deviceList[i].type), deviceList[i].keymapShift);
        }
    }

    // after scan, reset channel last read
    change_channel_reset_last_read();
}
