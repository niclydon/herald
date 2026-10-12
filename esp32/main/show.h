// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "noise_control.h"
cJSON *show_command(const char *, cJSON *, const char *,
                    noise_ctrl_session_generation_t);
bool show_request_stop(void);
