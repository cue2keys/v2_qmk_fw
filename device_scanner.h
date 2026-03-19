#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "keycodes.h"
#include "debug.h"
#include "pendant_reader.h"

typedef struct {
    Pendant_v2_ModuleType_enum_t type;
    uint8_t                      address;
    uint64_t                     UID;
} IdentifiedDevice;

typedef struct {
    Pendant_v2_ModuleType_enum_t type;
    uint8_t                      ch;
    uint8_t                      address;
    uint16_t                     keymapShift;
    uint64_t                     UID;
    uint8_t                      consecutive_errors;
} DeviceList;

void uid_to_string(uint64_t uid, char *buffer, size_t buffer_size);
void do_scan(void);

Pendant_v2_SetI2CAddrCmdErrorCode_enum_t update_v2_address(uint8_t channel, uint8_t old_address, uint8_t new_address);
