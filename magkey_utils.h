#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifndef MAGKEY_REFERENCE_BITS
#    define MAGKEY_REFERENCE_BITS 12
#endif

#ifndef MAGKEY_DATA_BYTES_MAX
#    define MAGKEY_DATA_BYTES_MAX 6
#endif

uint8_t magkey_data_bytes_from_config(uint8_t config_value);
uint8_t magkey_bits_per_value(uint8_t data_bytes);
uint8_t magkey_value_shift(uint8_t bits_per_value);
uint8_t magkey_register_for_bytes(uint8_t data_bytes);

void magkey_reset_cached_values(void);
bool magkey_get_latest_value(uint8_t row, uint8_t col, uint16_t *out_value);
bool magkey_update_pressed(uint16_t device_index, uint8_t key_index, uint16_t value, uint8_t row, uint8_t col, uint8_t value_shift);
void magkey_set_last_values(uint16_t device_index, const uint16_t values[4]);
