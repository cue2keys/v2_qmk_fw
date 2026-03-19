#pragma once

// PMW3610 modular driver (bit-banged SPI, multi-sensor)
// Mirrors the modular_adns5050.* interface for drop-in usage.

#include <stdbool.h>
#include <stdint.h>

// Default pin mappings for two modular sensors.
// Override at compile time if your wiring differs.
#ifndef MODULAR_PMW3610_SDIO_PINS
#    define MODULAR_PMW3610_SDIO_PINS {GP4, GP4, GP28, GP28}
#endif
#ifndef MODULAR_PMW3610_SCLK_PINS
#    define MODULAR_PMW3610_SCLK_PINS {GP6, GP6, GP26, GP26}
#endif
#ifndef MODULAR_PMW3610_CS_PINS
#    define MODULAR_PMW3610_CS_PINS {GP5, GP8, GP24, GP29}
#endif
#ifndef MODULAR_PMW3610_MOTION_PINS
#    define MODULAR_PMW3610_MOTION_PINS {GP7, GP7, GP27, GP27}
#endif

#ifndef MODULAR_PMW3610_SCLK_PINS
#    error "No clock pin defined -- missing MODULAR_PMW3610_SCLK_PINS"
#endif
#ifndef MODULAR_PMW3610_SDIO_PINS
#    error "No data pin defined -- missing MODULAR_PMW3610_SDIO_PINS"
#endif
#ifndef MODULAR_PMW3610_CS_PINS
#    error "No CS pin defined -- missing MODULAR_PMW3610_CS_PINS"
#endif
#ifndef MODULAR_PMW3610_MOTION_PINS
#    error "No RESET pin defined -- missing MODULAR_PMW3610_MOTION_PINS"
#endif

#define NUM_MODULAR_PMW3610 ARRAY_SIZE(((pin_t[])MODULAR_PMW3610_SCLK_PINS))

// Helper
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

// // CPI convenience constants (PMW3610 is 200-CPI steps, 200..3200)
// #define PMW3610_CPI_200 200
// #define PMW3610_CPI_400 400
// #define PMW3610_CPI_600 600
// #define PMW3610_CPI_800 800
// #define PMW3610_CPI_1000 1000
// #define PMW3610_CPI_1200 1200
// #define PMW3610_CPI_1600 1600
// #define PMW3610_CPI_2000 2000
// #define PMW3610_CPI_2400 2400
// #define PMW3610_CPI_3200 3200

// Default CPI for PMW3610
// actual value is defined by kb_config, so this is just a fallback value
#define PMW3610_CPI_INTERVAL 200
#define PMW3610_CPI_MIN 200
#define PMW3610_CPI_MAX 3200
#define PMW3610_DEFAULT_CPI 800

typedef struct {
    int16_t dx; // signed 12-bit, stored in 16-bit
    int16_t dy; // signed 12-bit, stored in 16-bit
} report_modular_pmw3610_t;

#ifdef POINTING_DEVICE_ENABLE

#    include "pointing_device.h"

// const pointing_device_driver_t modular_pmw3610_pointing_device_driver;

// High-level API (multi-sensor aggregation)
bool           modular_pmw3610_init(void);
void           modular_pmw3610_init_slot(uint8_t index);
bool           modular_pmw3610_probe(uint8_t index);
report_mouse_t modular_pmw3610_get_all_report(report_mouse_t mouse_report);
uint16_t       modular_pmw3610_get_all_cpi(void);
void           modular_pmw3610_set_all_cpi(uint16_t cpi);
void           modular_pmw3610_power_down_all(void);
void           modular_pmw3610_wake_up_all(bool connected_only);
void           modular_pmw3610_check_timeout(void);
uint8_t        modular_pmw3610_get_connected_count(void);
bool           modular_pmw3610_is_connected(uint8_t index);
uint8_t        modular_pmw3610_get_connected_flags(uint8_t *out, uint8_t max);
void           modular_pmw3610_set_led_off_length(uint32_t length_ms);
bool           modular_pmw3610_check_motion(uint8_t index);
bool           modular_pmw3610_check_motion_all(void);

// Per-sensor API
void                     modular_pmw3610_wake_up(uint8_t index, bool connected_only);
void                     modular_pmw3610_sync(uint8_t index);
uint8_t                  modular_pmw3610_serial_read(uint8_t index);
void                     modular_pmw3610_serial_write(uint8_t index, uint8_t data);
uint8_t                  modular_pmw3610_read_reg(uint8_t index, uint8_t reg_addr);
void                     modular_pmw3610_write_reg(uint8_t index, uint8_t reg_addr, uint8_t data);
report_modular_pmw3610_t modular_pmw3610_read_burst(uint8_t index);
void                     modular_pmw3610_set_cpi(uint8_t index, uint16_t cpi);
uint16_t                 modular_pmw3610_get_cpi(uint8_t index);
bool                     modular_pmw3610_check_signature(uint8_t index);
void                     modular_pmw3610_power_down(uint8_t index);
report_mouse_t           modular_pmw3610_get_report(uint8_t index, report_mouse_t mouse_report);
void                     modular_pmw3610_set_angle(uint8_t index, uint16_t angle);
void                     modular_pmw3610_add_angle(uint8_t index, int16_t angle);
uint16_t                 modular_pmw3610_get_angle(uint8_t index);

// for customized per-sensor report processing
__attribute__((weak)) report_mouse_t modular_pmw3610_get_report_custom(uint8_t index, report_mouse_t mouse_report);

#endif
