#pragma once
#include "noise_control.h"
cJSON *ble_central_command(const char *command, cJSON *params,
                           const char *request_id,
                           noise_ctrl_session_generation_t generation);
void ble_central_init(void);
void ble_central_reset(void);
