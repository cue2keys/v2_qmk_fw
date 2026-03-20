#pragma once

#define FW_VERSION "2.0.1" // x-release-please-version
#define EECONFIG_KB_DATA_VERSION (0x01)

// see `wear_leveling_rp2040_flash_config.h`
// currently, using 16MB SPI flash
#define DYNAMIC_KEYMAP_EEPROM_MAX_ADDR 8192

// using W25Q128JVS as an EEPROM
#define EXTERNAL_EEPROM_PAGE_SIZE 256
#define EXTERNAL_EEPROM_BYTE_COUNT (65536 * EXTERNAL_EEPROM_PAGE_SIZE)
#define EXTERNAL_EEPROM_ADDRESS_SIZE 4

#define EECONFIG_KB_DATA_SIZE 2048 // 2'KB for kb_config_t

#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET               // Activates the double-tap behavior
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 1000U // Timeout window in ms in which the double tap can occur.
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED GP25      // Specify a optional status led by GPIO number which blinks when entering the bootloader

#define I2C_DRIVER I2CD1
#define I2C1_SDA_PIN GP2
#define I2C1_SCL_PIN GP3

#define ENCODER_A_PINS {NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN}
#define ENCODER_B_PINS {NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN}

#define POINTING_DEVICE_AUTO_MOUSE_ENABLE
#define AUTO_MOUSE_DEFAULT_LAYER 3

// use _PER_KEY option for dynamic settings in `get_permissive_hold`
#define PERMISSIVE_HOLD_PER_KEY
#define TAPPING_TERM_PER_KEY

// pmw3610 returns larger x/y value than +-127
#define MOUSE_EXTENDED_REPORT 1
#define WHEEL_EXTENDED_REPORT 1

#define OLED_DISPLAY_128X64 1

#define DEBUG_MATRIX_SCAN_RATE

#define F_SCL 400000UL

#define OLED_UPDATE_INTERVAL 50

// 250 per second looks good enough
#define POINTING_DEVICE_TASK_THROTTLE_MS 4
