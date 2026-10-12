// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "noise_control.h"
#define HERALD_LAN_GENERATION UINT64_MAX
#ifdef __cplusplus
extern "C" {
#endif
void herald_lan_start(void);
bool herald_lan_has_authenticated_client(void);
void herald_lan_result(const char *id, cJSON *result); // takes ownership
void noise_ctrl_lan_invoke(const char *json);
#ifdef __cplusplus
}
#endif
