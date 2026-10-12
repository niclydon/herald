// SPDX-License-Identifier: Apache-2.0
#include "show.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "show_timeline.h"
#include "speaker.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static atomic_int state; /* 0 idle, 1 playing/preparing, 2 stopping */
typedef struct {
  char *id;
  noise_ctrl_session_generation_t gen;
  cJSON *params;
  show_plan_t plan;
  uint8_t *audio;
  size_t bytes;
  atomic_bool audio_done, audio_ok;
  int64_t started;
} show_job_t;
static cJSON *error(const char *code, const char *message) {
  cJSON *r = cJSON_CreateObject(), *e = cJSON_AddObjectToObject(r, "error");
  cJSON_AddBoolToObject(r, "ok", false);
  cJSON_AddStringToObject(e, "code", code);
  cJSON_AddStringToObject(e, "message", message);
  return r;
}
static cJSON *success(int64_t duration, bool stopped) {
  cJSON *r = cJSON_CreateObject(), *p = cJSON_AddObjectToObject(r, "payload");
  cJSON_AddBoolToObject(r, "ok", true);
  cJSON_AddNumberToObject(p, "duration_ms", duration);
  cJSON_AddBoolToObject(p, "stopped", stopped);
  return r;
}
bool show_request_stop(void) {
  taskENTER_CRITICAL(&state_lock);
  bool running = atomic_load(&state) != 0;
  if (running) {
    atomic_store(&state, 2);
    speaker_show_cancel();
    remote_led_show_cancel();
  }
  taskEXIT_CRITICAL(&state_lock);
  return running;
}
static void on_audio_start(int64_t at, void *arg) {
  show_job_t *j = arg;
  taskENTER_CRITICAL(&state_lock);
  j->started = at;
  taskEXIT_CRITICAL(&state_lock);
}
static void audio_task(void *arg) {
  show_job_t *j = arg;
  bool ok =
      atomic_load(&state) == 1 &&
      speaker_show_play(j->audio, j->bytes, j->plan.volume, on_audio_start, j);
  atomic_store(&j->audio_ok, ok);
  atomic_store(&j->audio_done, true);
  vTaskDelete(NULL);
}
static void coordinator(void *arg) {
  show_job_t *j = arg;
  cJSON *result = NULL;
  int64_t first = 0;
  bool child = false;
  const char *url =
      cJSON_GetObjectItemCaseSensitive(j->params, "audio_url")->valuestring;
  if (atomic_load(&state) == 1)
    j->audio = speaker_show_prepare(url, &j->bytes);
  if (!j->audio && atomic_load(&state) == 1)
    result = error("audio_prepare_failed",
                   "Unable to fetch/validate HTTPS PCM16 WAV (maximum 2 MiB)");
  while (!result && atomic_load(&state) == 1) {
    taskENTER_CRITICAL(&state_lock);
    j->started = 0;
    taskEXIT_CRITICAL(&state_lock);
    atomic_store(&j->audio_done, false);
    atomic_store(&j->audio_ok, false);
    if (xTaskCreate(audio_task, "Hal.show.audio", 6144, j, 3, NULL) != pdPASS) {
      result = error("out_of_memory", "Cannot start show audio task");
      break;
    }
    child = true;
    int selected = -1;
    for (;;) {
      if (atomic_load(&state) != 1 || speaker_show_cancelled()) {
        show_request_stop();
        break;
      }
      taskENTER_CRITICAL(&state_lock);
      int64_t start = j->started;
      taskEXIT_CRITICAL(&state_lock);
      if (start) {
        if (!first)
          first = start;
        int64_t elapsed = (esp_timer_get_time() - start) / 1000;
        int phase = show_phase_at(&j->plan, elapsed);
        if (phase != selected) {
          if (phase < 0)
            remote_led_show_hold();
          else if (!remote_led_show_phase(
                       &j->plan.phases[phase].pattern,
                       start + (int64_t)j->plan.phases[phase].start * 1000)) {
            if (atomic_load(&state) == 1)
              result = error("led_failed", "Unable to apply show LED phase");
            break;
          }
          selected = phase;
        }
        if (atomic_load(&j->audio_done) && elapsed >= j->plan.end)
          break;
      }
      if (atomic_load(&j->audio_done) && !atomic_load(&j->audio_ok)) {
        result = error("audio_failed", "Show audio playback failed");
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (result || atomic_load(&state) != 1)
      speaker_show_cancel();
    remote_led_show_hold();
    while (!atomic_load(&j->audio_done))
      vTaskDelay(pdMS_TO_TICKS(10));
    child = false;
    if (!result && atomic_load(&state) == 1 && !atomic_load(&j->audio_ok))
      result = error("audio_failed", "Show audio playback failed");
    if (!j->plan.loop)
      break;
  }
  if (child) {
    speaker_show_cancel();
    while (!atomic_load(&j->audio_done))
      vTaskDelay(pdMS_TO_TICKS(10));
  }
  remote_led_show_hold();
  bool stopped = atomic_load(&state) == 2;
  /* Keep ownership until both LEDs are off; never clear between loop passes
   * or on cancellation/failure. Only the coordinator can block here. */
  if (!result && !stopped && !j->plan.loop) {
    if (!remote_led_show_clear())
      result = error("led_failed", "Unable to clear LEDs after show");
    stopped = atomic_load(&state) == 2;
  }
  if (!result)
    result =
        success(first ? (esp_timer_get_time() - first) / 1000 : 0, stopped);
  ESP_LOGI("link.show", "show completed; stopped=%d", stopped);
  free(j->audio);
  remote_led_show_release();
  speaker_show_release();
  taskENTER_CRITICAL(&state_lock);
  atomic_store(&state, 0);
  taskEXIT_CRITICAL(&state_lock);
  noise_ctrl_send_command_result(j->gen, j->id, result);
  cJSON_Delete(j->params);
  free(j->id);
  free(j);
  vTaskDelete(NULL);
}
cJSON *show_command(const char *cmd, cJSON *params, const char *id,
                    noise_ctrl_session_generation_t gen) {
  if (!strcmp(cmd, "show.stop")) {
    if (params && (!cJSON_IsObject(params) || cJSON_GetArraySize(params)))
      return error("invalid_params", "show.stop takes no parameters");
    bool reactive=false;
#if CONFIG_HOMEHUB_LED_REACTIVE
    reactive=remote_led_reactive_stop();
#endif
    if (!show_request_stop() && !reactive)
      return error("not_running", "No show is running");
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return r;
  }
  show_job_t *j = calloc(1, sizeof(*j));
  if (!j)
    return error("out_of_memory", "Cannot allocate show");
  const char *bad = show_parse(params, &j->plan);
  if (bad) {
    free(j);
    return error("invalid_params", bad);
  }
  if (!id || !id[0] || strlen(id) > 128 || !gen) {
    free(j);
    return error("invalid_session", "Missing show request/session");
  }
  j->id = strdup(id);
  j->params = cJSON_Duplicate(params, true);
  j->gen = gen;
  cJSON *ack = cJSON_CreateObject();
  if (!j->id || !j->params || !ack ||
      !cJSON_AddBoolToObject(ack, "_async", true)) {
    cJSON_Delete(ack);
    free(j->id);
    cJSON_Delete(j->params);
    free(j);
    return error("out_of_memory", "Cannot copy show");
  }
  taskENTER_CRITICAL(&state_lock);
  bool reserved = atomic_load(&state) == 0;
  if (reserved)
    atomic_store(&state, 1);
  taskEXIT_CRITICAL(&state_lock);
  bool audio = false, led = false;
  const char *message = "A show is running; call show.stop first";
  if (reserved) {
    audio = speaker_show_claim();
    if (audio)
      led = remote_led_show_claim();
    message = audio ? "LED engine busy; retry"
                    : "Speaker busy; stop current audio first";
    if (audio && led) {
      if (xTaskCreate(coordinator, "Hal.show", 6144, j, 3, NULL) == pdPASS)
        return ack;
      message = "Cannot start show coordinator";
    }
  }
  if (led)
    remote_led_show_release();
  if (audio)
    speaker_show_release();
  if (reserved) {
    taskENTER_CRITICAL(&state_lock);
    atomic_store(&state, 0);
    taskEXIT_CRITICAL(&state_lock);
  }
  cJSON_Delete(ack);
  free(j->id);
  cJSON_Delete(j->params);
  free(j);
  return error("busy", message);
}
