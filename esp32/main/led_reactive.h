#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "cJSON.h"
typedef struct { int mode, smoothing_ms, colors[2][3]; float sensitivity; } reactive_config_t;
typedef struct { float envelope[2], agc[2], flux_mean, previous[64]; int64_t beat_until; } reactive_state_t;
const char *reactive_parse(cJSON *, reactive_config_t *);
void reactive_analyze(const int16_t *, size_t, unsigned, float *, float *, float *, float *);
void reactive_levels(reactive_state_t *, const reactive_config_t *, float, const float *, float, float, int64_t, float *);
#ifdef __cplusplus
extern "C" {
#endif
bool led_reactive_init(void);
void led_reactive_begin(const reactive_config_t *);
void led_reactive_end(void);
void led_reactive_tap(const int16_t *, size_t, unsigned);
void led_reactive_render(int64_t);
#ifdef __cplusplus
}
#endif
