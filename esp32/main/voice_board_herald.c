// SPDX-License-Identifier: Apache-2.0
// Herald voice board: the SDK push-to-talk stack (voice.c, voice_player.c)
// on Herald's own audio hardware instead of an XMOS front end.
//
// Microphone: the ZTS6631 module on I2S_NUM_1, through mic.c's streaming
// API (16 kHz mono PCM16, left slot, top 16 bits of the 32-bit word: the
// conversion the reference boards use). Speaker: voice_player's 48 kHz
// stereo int32 frames downmixed to mono PCM16, volume applied, and
// written through speaker.c's NS4168 channel claim.
//
// This board has no amp enable pin (the NS4168 is always powered from
// 3V3), no mute switch and no volume dial: amp() brackets the speaker
// claim instead (voice_player calls it around each reply), muted() is
// false and dial_steps() is 0, the reSpeaker Lite precedents.
#include "voice_board.h"
#include "mic.h"
#include "speaker.h"
#include "esp_log.h"
#include <stdatomic.h>

static const char *TAG = "link.voice_board";
static atomic_int s_volume = 100;
/* Conversion scratch: 256 stereo frames at a time, static so the voice
 * player task's small stack stays out of it. */
static int16_t s_pcm[256 * 2];

esp_err_t voice_board_init(void) {
    /* speaker.c owns the NS4168 channel (speaker_start) and mic.c the
     * ZTS6631 channel; both come up independently of the voice stack and
     * report their own failures, so there is nothing to set up here. */
    return ESP_OK;
}

esp_err_t voice_board_mic_start(void) { return mic_stream_start(); }

void voice_board_mic_stop(void) { mic_stream_stop(); }

size_t voice_board_mic_read(int16_t *pcm, size_t frames, int *peak) {
    return mic_stream_read(pcm, frames, peak);
}

esp_err_t voice_board_speaker_write(const int32_t *frames, size_t count) {
    int volume = atomic_load(&s_volume);
    while (count) {
        size_t n = count > 256 ? 256 : count;
        for (size_t i = 0; i < n; i++) {
            int l = (int)(frames[2 * i] >> 16);
            int r = (int)(frames[2 * i + 1] >> 16);
            int s = (l + r) / 2 * volume / 100;
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            s_pcm[2 * i] = s_pcm[2 * i + 1] = (int16_t)s;
        }
        if (!speaker_voice_write(s_pcm, n * 2 * sizeof(int16_t))) return ESP_FAIL;
        frames += 2 * n;
        count -= n;
    }
    return ESP_OK;
}

void voice_board_amp(bool on) {
    if (on) {
        esp_err_t e = speaker_voice_begin();
        if (e != ESP_OK)
            ESP_LOGW(TAG, "speaker claim failed: %s", esp_err_to_name(e));
    } else {
        speaker_voice_end();
    }
}

void voice_board_set_volume(int percent) {
    /* The reference boards program a codec's digital gain; Herald has no
     * codec, so the value is stored here and applied as gain in
     * voice_board_speaker_write's conversion. */
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    atomic_store(&s_volume, percent);
}

bool voice_board_muted(void) { return false; }

int voice_board_dial_steps(void) { return 0; }
