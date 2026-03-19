#pragma once

#include <stdint.h>
#include "pendant_builder.h"

#ifdef VIA_ENABLE

void via_custom_value_command_kb(uint8_t *data, uint8_t length);
void get_param(Pendant_v2_Pkt_t *pkt);
void set_param(Pendant_v2_Pkt_t *pkt);
void save_param(void);

void apply_set_param_side_effect(uint8_t value_id);
void send_device_list(Pendant_v2_Pkt_t *pkt);

#endif
