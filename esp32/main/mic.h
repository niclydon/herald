// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "noise_control.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
cJSON *mic_command(const char *command, cJSON *params,
                   const char *request_id, noise_ctrl_session_generation_t generation);
/* Streaming capture for the voice stack (voice_board_herald.c). Shares the
 * single I2S_NUM_1 channel with mic.level: one owner at a time, and the
 * side that loses the race gets a busy error. Samples are 16 kHz mono
 * PCM16 converted from the left slot (top 16 bits of the 32-bit word). */
esp_err_t mic_stream_start(void);
size_t mic_stream_read(int16_t *pcm, size_t frames, int *peak);
void mic_stream_stop(void);
#ifdef __cplusplus
}
#endif
