// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "noise_control.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void speaker_start(void);
cJSON *speaker_stop_command(cJSON *params);
cJSON *speaker_command(const char *command, cJSON *params,
                       const char *request_id, noise_ctrl_session_generation_t generation);
/* Raw playback for the voice stack (voice_board_herald.c). begin claims
 * the NS4168 channel and runs it at 48 kHz until end; it fails with
 * ESP_ERR_TIMEOUT while a speaker command owns the channel, the same
 * busy rule the speaker commands apply to each other. write takes stereo
 * PCM16 at 48 kHz. */
esp_err_t speaker_voice_begin(void);
bool speaker_voice_write(const int16_t *pcm, size_t bytes);
void speaker_voice_end(void);
#ifdef __cplusplus
}
#endif

#if CONFIG_HOMEHUB_SHOW
#include <stdint.h>
bool speaker_show_claim(void);
void speaker_show_release(void);
void speaker_show_cancel(void);
bool speaker_show_cancelled(void);
uint8_t *speaker_show_prepare(const char *url, size_t *n);
bool speaker_show_play(const uint8_t *data,size_t n,int volume,void (*started)(int64_t,void *),void *arg);
#endif
