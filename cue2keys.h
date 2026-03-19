#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "quantum.h"
#include "kb_config.h"

#define I2C_READ_ERROR_RESCAN_THRESHOLD 5

#ifdef POINTING_DEVICE_ENABLE
// #    include "drivers/modular_adns5050.h"
#    include "drivers/modular_pmw3610.h"
#endif
