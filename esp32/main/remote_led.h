#pragma once
#include "noise_control.h"
#ifdef __cplusplus
extern "C" {
#endif
void remote_led_init(void);
void remote_led_poll(void);
void remote_led_demo_claim(void);
cJSON *remote_led_command(const char *,cJSON *,const char *,noise_ctrl_session_generation_t);
#ifdef __cplusplus
}
#endif
