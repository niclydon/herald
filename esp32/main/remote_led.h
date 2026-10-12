#pragma once
#include "noise_control.h"
#ifdef __cplusplus
extern "C" {
#endif
void remote_led_init(void);
void remote_led_poll(void);
void remote_led_demo_claim(void);
#if CONFIG_HOMEHUB_LED_REACTIVE
bool remote_led_reactive_stop(void);
#endif
cJSON *remote_led_command(const char *,cJSON *,const char *,noise_ctrl_session_generation_t);
#ifdef __cplusplus
}
#endif

#if CONFIG_HOMEHUB_SHOW
typedef struct { int mode,count,speed,brightness; int colors[16][3]; } remote_led_pattern_t;
bool remote_led_show_claim(void);
bool remote_led_show_phase(const remote_led_pattern_t *p,int64_t started);
void remote_led_show_hold(void);
/* Coordinator only: clear while retaining ownership, before completion. */
bool remote_led_show_clear(void);
void remote_led_show_cancel(void);
void remote_led_show_release(void);
#endif
