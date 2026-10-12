// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "cJSON.h"
#include "remote_led.h"
#include <math.h>
#include <string.h>
#define SHOW_PHASES_MAX 32
typedef struct {
  remote_led_pattern_t pattern;
  int start, duration;
} show_phase_t;
typedef struct {
  show_phase_t phases[SHOW_PHASES_MAX];
  int count, volume;
  bool loop;
  int64_t end;
} show_plan_t;
static inline bool show_int(cJSON *p, const char *k, int min, int max, int def,
                            bool required, int *out) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(p, k);
  if (!v) {
    *out = def;
    return !required;
  }
  if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < min ||
      v->valuedouble > max || floor(v->valuedouble) != v->valuedouble)
    return false;
  *out = v->valueint;
  return true;
}
static inline bool show_keys(cJSON *p, const char *keys) {
  if (!cJSON_IsObject(p))
    return false;
  cJSON *v;
  cJSON_ArrayForEach(v, p) {
    if (!v->string || strlen(v->string) > 24)
      return false;
    char key[28];
    size_t n = strlen(v->string);
    key[0] = '|';
    memcpy(key + 1, v->string, n);
    key[n + 1] = '|';
    key[n + 2] = 0;
    if (!strstr(keys, key))
      return false;
    for (cJSON *b = v->next; b; b = b->next)
      if (b->string && !strcmp(v->string, b->string))
        return false;
  }
  return true;
}
static inline const char *show_parse(cJSON *p, show_plan_t *out) {
  memset(out, 0, sizeof(*out));
  if (!show_keys(p, "|audio_url|timeline|volume|loop|"))
    return "Unknown, duplicate or invalid show parameters";
  cJSON *url = cJSON_GetObjectItemCaseSensitive(p, "audio_url"),
        *timeline = cJSON_GetObjectItemCaseSensitive(p, "timeline"),
        *loop = cJSON_GetObjectItemCaseSensitive(p, "loop");
  if (!cJSON_IsString(url) || strncmp(url->valuestring, "https://", 8) ||
      strlen(url->valuestring) <= 8 || strlen(url->valuestring) > 2048)
    return "audio_url must be HTTPS, at most 2048 bytes";
  const char *authority = url->valuestring + 8;
  size_t host_len = strcspn(authority, "/?#");
  if (!host_len || memchr(authority, '@', host_len))
    return "audio_url needs a host without embedded credentials";
  for (const char *c = url->valuestring; *c; c++)
    if ((unsigned char)*c <= 32 || *c == '\\')
      return "Invalid URL character";
  if (!show_int(p, "volume", 0, 100, 75, false, &out->volume) ||
      (loop && !cJSON_IsBool(loop)))
    return "volume must be 0-100 and loop must be boolean";
  out->loop = cJSON_IsTrue(loop);
  if (!cJSON_IsArray(timeline) ||
      (out->count = cJSON_GetArraySize(timeline)) < 1 ||
      out->count > SHOW_PHASES_MAX)
    return "timeline requires 1-32 ordered phases";
  for (int i = 0; i < out->count; i++) {
    cJSON *phase = cJSON_GetArrayItem(timeline, i);
    show_phase_t *f = &out->phases[i];
    remote_led_pattern_t *r = &f->pattern;
    if (!show_keys(phase,
                   "|colors|pattern|start_ms|duration_ms|speed_ms|brightness|"))
      return "Invalid phase fields";
    if (!show_int(phase, "start_ms", 0, 86400000, 0, true, &f->start) ||
        !show_int(phase, "duration_ms", 1, 86400000, 0, true, &f->duration) ||
        !show_int(phase, "speed_ms", 20, 60000, 300, false, &r->speed) ||
        !show_int(phase, "brightness", 0, 100, 100, false, &r->brightness))
      return "Invalid phase timing or brightness";
    if (i && f->start < out->phases[i - 1].start)
      return "timeline must be ordered by start_ms";
    int64_t end = (int64_t)f->start + f->duration;
    if (end > out->end)
      out->end = end;
    cJSON *mode = cJSON_GetObjectItemCaseSensitive(phase, "pattern"),
          *colors = cJSON_GetObjectItemCaseSensitive(phase, "colors");
    if (!cJSON_IsString(mode) || !cJSON_IsArray(colors) ||
        (r->count = cJSON_GetArraySize(colors)) < 1 || r->count > 16)
      return "Each phase requires a pattern and 1-16 RGB colors";
    r->mode = -1;
    const char *names[] = {"chase", "alternate", "fade", "sparkle"};
    for (int k = 0; k < 4; k++)
      if (!strcmp(mode->valuestring, names[k]))
        r->mode = k;
    if (r->mode < 0)
      return "Unknown LED pattern";
    for (int k = 0; k < r->count; k++) {
      cJSON *rgb = cJSON_GetArrayItem(colors, k);
      if (!cJSON_IsArray(rgb) || cJSON_GetArraySize(rgb) != 3)
        return "Colors must be [r,g,b]";
      for (int c = 0; c < 3; c++) {
        cJSON *v = cJSON_GetArrayItem(rgb, c);
        if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) ||
            v->valuedouble < 0 || v->valuedouble > 255 ||
            floor(v->valuedouble) != v->valuedouble)
          return "RGB channels must be integers 0-255";
        r->colors[k][c] = v->valueint;
      }
    }
  }
  return NULL;
}
static inline int show_phase_at(const show_plan_t *p, int64_t elapsed) {
  int selected = -1;
  for (int i = 0; i < p->count; i++)
    if (elapsed >= p->phases[i].start &&
        elapsed < (int64_t)p->phases[i].start + p->phases[i].duration)
      selected = i;
  return selected;
}
