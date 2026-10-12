// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "noise_control.h"
#ifdef __cplusplus
extern "C" {
#endif
void speaker_start(void);
cJSON *speaker_stop_command(cJSON *params);
cJSON *speaker_command(const char *command, cJSON *params,
                       const char *request_id, noise_ctrl_session_generation_t generation);
#ifdef __cplusplus
}
#endif
