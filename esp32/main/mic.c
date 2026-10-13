// SPDX-License-Identifier: Apache-2.0
// Herald microphone: emakefun ZTS663 I2S MEMS module (ZTS6631 chip).
// Wiring (see esp32/devices/herald-wiring.md): BCLK=GPIO14, LRC=GPIO13,
// DATA=GPIO10, power from the board 5V pin (the module regulates 5V down
// to 3.3V onboard). The module is believed hardwired for I2S left channel
// (SELECT low, CONFIG float); it outputs 24-bit samples in a 32-bit
// Philips I2S slot, MSB-aligned, so the low byte of each word is padding.
// Firmware 999.0.7 read only the left slot and measured exact digital
// silence even with the module powered, so this build captures BOTH
// slots and reports per-slot statistics plus the largest raw word seen:
// that distinguishes wrong-slot, wrong-bit-position and no-output cases.
// This is a level meter only: mic.level captures a short window, reports
// RMS / peak / dBFS and discards the audio. Nothing is stored or streamed.
// The speaker owns I2S_NUM_0; the microphone owns I2S_NUM_1 so the two
// never share clocks or a channel.
#include "sdkconfig.h"
#include "mic.h"
#include "cJSON.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define MIC_SAMPLE_RATE 16000
#define MIC_DEFAULT_DURATION_MS 1000
#define MIC_MIN_DURATION_MS 100
#define MIC_MAX_DURATION_MS 3000
#define MIC_SETTLE_FRAMES 1600 /* 100 ms discarded after enable */
#define MIC_CHUNK_FRAMES 128    /* stereo frames per read; 2 words each */

static const char *TAG = "link.mic";
static i2s_chan_handle_t rx;
static atomic_bool capture_busy;

typedef struct {
    char *id;
    noise_ctrl_session_generation_t generation;
    int duration_ms;
} mic_job_t;

static cJSON *command_error(const char *code, const char *msg) {
    cJSON *r = cJSON_CreateObject(), *e = cJSON_AddObjectToObject(r, "error");
    cJSON_AddBoolToObject(r, "ok", false);
    cJSON_AddStringToObject(e, "code", code);
    cJSON_AddStringToObject(e, "message", msg);
    return r;
}

static cJSON *capture_error_result(const char *code, const char *msg) {
    return command_error(code, msg);
}

/* Lazy I2S_NUM_1 RX setup. Called only from the capture worker, which the
 * busy flag serializes, so no extra locking is needed here. The channel
 * stays allocated but disabled between captures. */
static esp_err_t mic_ensure_channel(void) {
    if (rx) return ESP_OK;
    i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    esp_err_t e = i2s_new_channel(&ch, NULL, &rx);
    if (e != ESP_OK) { rx = NULL; return e; }
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = GPIO_NUM_14, .ws = GPIO_NUM_13,
                     .dout = I2S_GPIO_UNUSED, .din = GPIO_NUM_10,
                     .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}},
    };
    e = i2s_channel_init_std_mode(rx, &cfg);
    if (e != ESP_OK) { i2s_del_channel(rx); rx = NULL; return e; }
    ESP_LOGI(TAG, "ZTS6631 ready: BCLK=14 LRC=13 DATA=10, %d Hz, 32-bit slots, stereo capture", MIC_SAMPLE_RATE);
    return ESP_OK;
}

/* Reads exactly `words` 32-bit slot words (2 per stereo frame). */
static bool read_words(int32_t *buf, size_t words) {
    size_t want = words * sizeof(int32_t), got_total = 0;
    while (got_total < want) {
        size_t got = 0;
        esp_err_t e = i2s_channel_read(rx, (uint8_t *)buf + got_total, want - got_total, &got, 1000);
        if (e != ESP_OK || got == 0) return false;
        got_total += got;
    }
    return true;
}

/* Streaming capture for push-to-talk (voice_board_herald.c). Shares the
 * channel and the busy flag with mic.level: whichever side starts first
 * owns the microphone and the other gets a busy error, so a voice capture
 * can never race a level measurement onto I2S_NUM_1. */
static int32_t stream_words[MIC_CHUNK_FRAMES * 2];
static atomic_bool stream_active;

esp_err_t mic_stream_start(void) {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&capture_busy, &expected, true))
        return ESP_ERR_INVALID_STATE; /* a capture already owns the mic */
    esp_err_t e = mic_ensure_channel();
    if (e == ESP_OK) e = i2s_channel_enable(rx);
    if (e == ESP_OK) {
        /* Same settle discard as mic.level, so the module's clocks are
         * stable before the first kept sample. */
        for (size_t left = MIC_SETTLE_FRAMES; left > 0;) {
            size_t n = left > MIC_CHUNK_FRAMES ? MIC_CHUNK_FRAMES : left;
            if (!read_words(stream_words, n * 2)) { e = ESP_FAIL; break; }
            left -= n;
        }
    }
    if (e != ESP_OK) {
        if (rx) i2s_channel_disable(rx);
        atomic_store(&capture_busy, false);
        return e;
    }
    atomic_store(&stream_active, true);
    return ESP_OK;
}

/* Blocks until `frames` samples are read or a read fails; returns the
 * number of samples stored in `pcm` and their peak absolute value. */
size_t mic_stream_read(int16_t *pcm, size_t frames, int *peak) {
    if (peak) *peak = 0;
    if (!pcm || !atomic_load(&stream_active)) return 0;
    size_t done = 0;
    while (done < frames) {
        size_t n = frames - done > MIC_CHUNK_FRAMES ? MIC_CHUNK_FRAMES : frames - done;
        if (!read_words(stream_words, n * 2)) break;
        for (size_t i = 0; i < n; i++) {
            /* Left slot, top 16 bits of the 32-bit word: the conversion
             * the SDK's reference voice boards use. */
            int16_t s = (int16_t)(stream_words[2 * i] >> 16);
            pcm[done + i] = s;
            if (peak) {
                int a = s < 0 ? -(int)s : (int)s;
                if (a > *peak) *peak = a;
            }
        }
        done += n;
    }
    return done;
}

void mic_stream_stop(void) {
    if (!atomic_exchange(&stream_active, false)) return;
    if (rx) i2s_channel_disable(rx);
    atomic_store(&capture_busy, false);
}

static void mic_worker(void *arg) {
    mic_job_t *j = arg;
    cJSON *result = NULL;
    esp_err_t e = mic_ensure_channel();
    if (e != ESP_OK) {
        char msg[96];
        snprintf(msg, sizeof(msg), "microphone init failed: %s", esp_err_to_name(e));
        result = capture_error_result("i2s_error", msg);
        goto send;
    }
    e = i2s_channel_enable(rx);
    if (e != ESP_OK) {
        result = capture_error_result("i2s_error", "microphone channel did not enable");
        goto send;
    }
    {
        int32_t buf[MIC_CHUNK_FRAMES * 2];
        bool ok = true;
        /* Let the clocks and the module settle before measuring. */
        for (size_t left = MIC_SETTLE_FRAMES; ok && left > 0;) {
            size_t n = left > MIC_CHUNK_FRAMES ? MIC_CHUNK_FRAMES : left;
            ok = read_words(buf, n * 2);
            left -= n;
        }
        size_t target = (size_t)MIC_SAMPLE_RATE * (size_t)j->duration_ms / 1000, frames = 0;
        /* Per-slot stats: index 0 = left, 1 = right. */
        double sum_sq[2] = {0.0, 0.0}, slot_peak[2] = {0.0, 0.0};
        size_t nonzero[2] = {0, 0};
        double raw_peak = 0.0;
        while (ok && frames < target) {
            size_t n = target - frames > MIC_CHUNK_FRAMES ? MIC_CHUNK_FRAMES : target - frames;
            ok = read_words(buf, n * 2);
            if (!ok) break;
            for (size_t i = 0; i < n; i++) {
                for (int s = 0; s < 2; s++) {
                    int32_t raw = buf[2 * i + s];
                    if (raw != 0) nonzero[s]++;
                    double araw = fabs((double)raw);
                    if (araw > raw_peak) raw_peak = araw;
                    /* 24-bit sample, MSB-aligned in the 32-bit slot. */
                    double v = (double)(raw >> 8) / 8388608.0;
                    sum_sq[s] += v * v;
                    if (fabs(v) > slot_peak[s]) slot_peak[s] = fabs(v);
                }
            }
            frames += n;
        }
        i2s_channel_disable(rx);
        if (!ok) {
            result = capture_error_result("capture_failed", "microphone read failed or timed out");
            goto send;
        }
        /* Top-level rms/peak/dbfs/clipped stay the left slot's, as in
         * 999.0.7; "channel" names the slot with the higher peak and the
         * "slots" object carries both slots in full. */
        double rms = frames ? sqrt(sum_sq[0] / (double)frames) : 0.0;
        double peak = slot_peak[0];
        double dbfs = rms > 0.0 ? 20.0 * log10(rms) : -120.0;
        cJSON *p = cJSON_CreateObject();
        if (!p) { result = capture_error_result("out_of_memory", "Cannot build result"); goto send; }
        cJSON_AddNumberToObject(p, "sample_rate", MIC_SAMPLE_RATE);
        cJSON_AddNumberToObject(p, "frames", (double)frames);
        cJSON_AddNumberToObject(p, "duration_ms", j->duration_ms);
        cJSON_AddStringToObject(p, "channel", slot_peak[1] > slot_peak[0] ? "right" : "left");
        cJSON_AddNumberToObject(p, "rms", rms);
        cJSON_AddNumberToObject(p, "peak", peak);
        cJSON_AddNumberToObject(p, "dbfs", floor(dbfs * 10.0 + 0.5) / 10.0);
        cJSON_AddBoolToObject(p, "clipped", peak >= 0.999);
        cJSON_AddNumberToObject(p, "raw_peak", raw_peak);
        cJSON *slots = cJSON_AddObjectToObject(p, "slots");
        const char *slot_names[2] = {"left", "right"};
        for (int s = 0; s < 2; s++) {
            cJSON *entry = cJSON_AddObjectToObject(slots, slot_names[s]);
            double s_rms = frames ? sqrt(sum_sq[s] / (double)frames) : 0.0;
            double s_dbfs = s_rms > 0.0 ? 20.0 * log10(s_rms) : -120.0;
            cJSON_AddNumberToObject(entry, "rms", s_rms);
            cJSON_AddNumberToObject(entry, "peak", slot_peak[s]);
            cJSON_AddNumberToObject(entry, "dbfs", floor(s_dbfs * 10.0 + 0.5) / 10.0);
            cJSON_AddNumberToObject(entry, "nonzero_frames", (double)nonzero[s]);
        }
        result = cJSON_CreateObject();
        cJSON_AddBoolToObject(result, "ok", true);
        cJSON_AddItemToObject(result, "payload", p);
        ESP_LOGI(TAG, "mic.level: frames=%u L rms=%.5f peak=%.5f R peak=%.5f raw_peak=%.0f",
                 (unsigned)frames, rms, slot_peak[0], slot_peak[1], raw_peak);
    }
send:
    /* noise_ctrl_send_command_result takes ownership of result. */
    if (result) noise_ctrl_send_command_result(j->generation, j->id, result);
    free(j->id);
    free(j);
    atomic_store(&capture_busy, false);
    vTaskDelete(NULL);
}

cJSON *mic_command(const char *command, cJSON *params, const char *id,
                   noise_ctrl_session_generation_t generation) {
    if (strcmp(command, "mic.level")) return command_error("unsupported", "Unknown microphone command");
    int duration_ms = MIC_DEFAULT_DURATION_MS;
    if (params) {
        if (!cJSON_IsObject(params)) return command_error("invalid_params", "Parameters must be an object");
        for (cJSON *v = params->child; v; v = v->next) {
            if (!v->string || strcmp(v->string, "duration_ms") || !cJSON_IsNumber(v) ||
                !isfinite(v->valuedouble) || v->valuedouble != v->valueint ||
                v->valueint < MIC_MIN_DURATION_MS || v->valueint > MIC_MAX_DURATION_MS)
                return command_error("invalid_params", "duration_ms must be an integer 100-3000");
            duration_ms = v->valueint;
        }
    }
    if (!id || !generation) return command_error("invalid_session", "Missing session");
    bool expected = false;
    if (!atomic_compare_exchange_strong(&capture_busy, &expected, true))
        return command_error("busy", "Microphone capture already in progress");
    mic_job_t *j = calloc(1, sizeof(*j));
    cJSON *ack = cJSON_CreateObject();
    if (j) { j->id = strdup(id); j->generation = generation; j->duration_ms = duration_ms; }
    if (ack) { cJSON_AddBoolToObject(ack, "_async", true); cJSON_AddBoolToObject(ack, "ok", true); }
    if (!j || !j->id || !ack || xTaskCreate(mic_worker, "Hal.mic", 5120, j, 2, NULL) != pdPASS) {
        if (j) { free(j->id); free(j); }
        cJSON_Delete(ack);
        atomic_store(&capture_busy, false);
        return command_error("out_of_memory", "Cannot start microphone worker");
    }
    return ack;
}
