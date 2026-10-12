#pragma once
#include "noise_control.h"
#ifdef __cplusplus
extern "C" {
#endif
void remote_preferences_init(void);
int remote_default_volume(void);
int remote_default_brightness(void);
cJSON *remote_device_command(const char *,cJSON *,const char *,noise_ctrl_session_generation_t);
#ifdef __cplusplus
}
#endif
