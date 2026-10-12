#include "freertos/task.h"
#include "sdkconfig.h"
#include "show.h"
#include "show_timeline.h"
#include "speaker.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static atomic_int live, completions, phases, plays, holds, clears;
static atomic_bool audio_owned, led_owned, cancelled, prepare_fail, play_fail;
static int audio_ms = 30;
static const char *fail_name;
static cJSON *last;
static pthread_mutex_t result_lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct {
  void (*fn)(void *);
  void *arg;
} task_t;
static void *thread(void *arg) {
  task_t *t = arg;
  void (*fn)(void *) = t->fn;
  void *a = t->arg;
  free(t);
  fn(a);
  atomic_fetch_sub(&live, 1);
  return NULL;
}
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg,
                unsigned priority, void *handle) {
  (void)stack;
  (void)priority;
  (void)handle;
  if (fail_name && !strcmp(fail_name, name))
    return 0;
  task_t *t = malloc(sizeof(*t));
  *t = (task_t){fn, arg};
  pthread_t p;
  atomic_fetch_add(&live, 1);
  assert(!pthread_create(&p, NULL, thread, t));
  pthread_detach(p);
  return 1;
}
void vTaskDelay(unsigned ms) {
  struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
  nanosleep(&t, NULL);
}
void vTaskDelete(void *t) { (void)t; }
int64_t esp_timer_get_time(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
bool speaker_show_claim(void) {
  bool expected = false;
  if (!atomic_compare_exchange_strong(&audio_owned, &expected, true))
    return false;
  atomic_store(&cancelled, false);
  return true;
}
void speaker_show_release(void) {
  assert(atomic_exchange(&audio_owned, false));
}
void speaker_show_cancel(void) { atomic_store(&cancelled, true); }
bool speaker_show_cancelled(void) { return atomic_load(&cancelled); }
uint8_t *speaker_show_prepare(const char *u, size_t *n) {
  assert(!strcmp(u, "https://example.com/show.wav"));
  *n = 1;
  return atomic_load(&prepare_fail) ? NULL : malloc(1);
}
bool speaker_show_play(const uint8_t *d, size_t n, int volume,
                       void (*started)(int64_t, void *), void *arg) {
  assert(d && n == 1 && volume == 75);
  atomic_fetch_add(&plays, 1);
  started(esp_timer_get_time(), arg);
  for (int i = 0; i < audio_ms; i += 5) {
    if (atomic_load(&cancelled))
      return false;
    vTaskDelay(5);
  }
  return !atomic_load(&play_fail);
}
bool remote_led_show_claim(void) {
  bool expected = false;
  return atomic_compare_exchange_strong(&led_owned, &expected, true);
}
bool remote_led_show_phase(const remote_led_pattern_t *p, int64_t started) {
  assert(p->count == 1 && started > 0);
  atomic_fetch_add(&phases, 1);
  return true;
}
void remote_led_show_hold(void) { atomic_fetch_add(&holds, 1); }
bool remote_led_show_clear(void) {
  assert(atomic_load(&led_owned));
  atomic_fetch_add(&clears, 1);
  return true;
}
void remote_led_show_cancel(void) {}
void remote_led_show_release(void) {
  assert(atomic_exchange(&led_owned, false));
}
void noise_ctrl_send_command_result(uint64_t gen, const char *id, cJSON *r) {
  assert(gen == 7 && !strcmp(id, "show-request"));
  /* Successful natural completions must clear before publishing the result. */
  if (cJSON_IsTrue(cJSON_GetObjectItem(r, "ok")) &&
      cJSON_IsFalse(cJSON_GetObjectItem(cJSON_GetObjectItem(r, "payload"), "stopped")))
    assert(atomic_load(&clears) == atomic_load(&completions) + 1);
  pthread_mutex_lock(&result_lock);
  cJSON_Delete(last);
  last = r;
  pthread_mutex_unlock(&result_lock);
  atomic_fetch_add(&completions, 1);
}
static const char *json =
    "{\"audio_url\":\"https://example.com/"
    "show.wav\",\"timeline\":[{\"pattern\":\"fade\",\"colors\":[[255,0,0]],"
    "\"start_ms\":0,\"duration_ms\":40},{\"pattern\":\"alternate\",\"colors\":[[0,0,255]],\"start_ms\":40,\"duration_ms\":40}]}";
static cJSON *invoke(const char *cmd, cJSON *p) {
  char id[] = "show-request";
  cJSON *r = show_command(cmd, p, id, 7);
  memset(id, 'x', sizeof(id) - 1);
  return r;
}
static void start(bool loop) {
  cJSON *p = cJSON_Parse(json);
  if (loop)
    cJSON_AddBoolToObject(p, "loop", true);
  cJSON *r = invoke("show.play", p);
  assert(cJSON_IsTrue(cJSON_GetObjectItem(r, "_async")));
  cJSON_Delete(r);
  cJSON_Delete(p);
}
static void wait_done(int expected) {
  for (int i = 0;
       i < 500 && (atomic_load(&completions) < expected || atomic_load(&live));
       i++)
    vTaskDelay(5);
  assert(atomic_load(&completions) == expected && !atomic_load(&live));
  assert(!atomic_load(&audio_owned) && !atomic_load(&led_owned));
}
static bool good(void) { return cJSON_IsTrue(cJSON_GetObjectItem(last, "ok")); }
int main(void) {
  show_plan_t *p = calloc(1, sizeof(*p));
  cJSON *params = cJSON_Parse(json);
  assert(!show_parse(params, p));
  assert(show_phase_at(p, 0) == 0 && show_phase_at(p, 79) == 1 &&
         show_phase_at(p, 80) == -1);
  p->count = 3;
  p->phases[0].duration = 100;
  p->phases[1].start = 20;
  p->phases[1].duration = 20;
  p->phases[2].start = 20;
  p->phases[2].duration = 10;
  assert(show_phase_at(p, 25) == 2 && show_phase_at(p, 35) == 1 &&
         show_phase_at(p, 50) == 0 && show_phase_at(p, 100) == -1);
  cJSON_ReplaceItemInObject(params, "audio_url",
                            cJSON_CreateString("http://example.com/show.wav"));
  assert(show_parse(params, p));
  cJSON_Delete(params);
  free(p);
  cJSON *r = invoke("show.stop", NULL);
  assert(cJSON_IsFalse(cJSON_GetObjectItem(r, "ok")));
  cJSON_Delete(r);
  fail_name = "Hal.show";
  params = cJSON_Parse(json);
  r = invoke("show.play", params);
  assert(cJSON_IsFalse(cJSON_GetObjectItem(r, "ok")));
  cJSON_Delete(r);
  cJSON_Delete(params);
  assert(!atomic_load(&audio_owned) && !atomic_load(&led_owned));
  fail_name = NULL;
  start(false);
  params = cJSON_Parse(json);
  r = invoke("show.play", params);
  assert(cJSON_IsFalse(cJSON_GetObjectItem(r, "ok")));
  cJSON_Delete(r);
  cJSON_Delete(params);
  wait_done(1);
  assert(good());
  assert(
      cJSON_GetObjectItem(cJSON_GetObjectItem(last, "payload"), "duration_ms")
          ->valuedouble >= 80);
  assert(atomic_load(&phases) == 2 && atomic_load(&clears) == 1);
  audio_ms = 130;
  start(false);
  wait_done(2);
  assert(good());
  assert(
      cJSON_GetObjectItem(cJSON_GetObjectItem(last, "payload"), "duration_ms")
          ->valuedouble >= 130);
  assert(atomic_load(&clears) == 2);
  audio_ms = 30;
  atomic_store(&prepare_fail, true);
  start(false);
  wait_done(3);
  assert(!good());
  atomic_store(&prepare_fail, false);
  atomic_store(&play_fail, true);
  start(false);
  wait_done(4);
  assert(!good());
  atomic_store(&play_fail, false);
  fail_name = "Hal.show.audio";
  start(false);
  wait_done(5);
  assert(!good());
  fail_name = NULL;
  int before = atomic_load(&plays);
  start(true);
  for (int i = 0; i < 100 && atomic_load(&plays) < before + 3; i++)
    vTaskDelay(5);
  assert(atomic_load(&plays) >= before + 3);
  assert(atomic_load(&clears) == 2);
  r = invoke("show.stop", NULL);
  assert(cJSON_IsTrue(cJSON_GetObjectItem(r, "ok")));
  cJSON_Delete(r);
  wait_done(6);
  assert(good() && cJSON_IsTrue(cJSON_GetObjectItem(
                       cJSON_GetObjectItem(last, "payload"), "stopped")));
  start(false);
  speaker_show_cancel();
  wait_done(7);
  assert(good());
  for (int i = 0; i < 20; i++) {
    start(i % 2 == 0);
    assert(show_request_stop());
    wait_done(8 + i);
    assert(good());
  }
  assert(atomic_load(&clears) == 2); /* failures and stops never clear */
  cJSON_Delete(last);
  puts("show overlap, concurrency, both completion orders, loop, stop, "
       "failures, ownership and 27 exactly-once results passed");
}
