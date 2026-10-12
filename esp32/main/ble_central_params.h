#pragma once
#include "cJSON.h"
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define CENTRAL_VALUE_MAX 512
/* Small pure helpers shared by firmware and host tests. */
static inline int central_hex_digit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
static inline bool central_mac(const char *s, uint8_t out[6]) {
  if (!s || strlen(s) != 17)
    return false;
  for (int i = 0; i < 6; i++) {
    int a = central_hex_digit(s[i * 3]), b = central_hex_digit(s[i * 3 + 1]);
    if (a < 0 || b < 0 || (i < 5 && s[i * 3 + 2] != ':'))
      return false;
    out[5 - i] = (a << 4) | b;
  }
  return true;
}
static inline bool central_uuid(const char *s) {
  if (!s)
    return false;
  size_t n = strlen(s);
  if (n != 4 && n != 8 && n != 36)
    return false;
  for (size_t i = 0; i < n; i++) {
    if (n == 36 && (i == 8 || i == 13 || i == 18 || i == 23)) {
      if (s[i] != '-')
        return false;
    } else if (central_hex_digit(s[i]) < 0)
      return false;
  }
  return true;
}
static inline bool central_hex(const char *s) {
  if (!s || strlen(s) > CENTRAL_VALUE_MAX * 2 || strlen(s) % 2)
    return false;
  for (size_t i = 0; s[i]; i++)
    if (central_hex_digit(s[i]) < 0)
      return false;
  return true;
}
static inline bool central_int(const cJSON *v, int low, int high) {
  return cJSON_IsNumber(v) && isfinite(v->valuedouble) &&
         v->valuedouble == floor(v->valuedouble) && v->valuedouble >= low &&
         v->valuedouble <= high;
}
/* Operation reservation and connection guard are atomic across Link/LAN
 * callers. */
static inline bool central_reserve(atomic_bool *busy) {
  bool expected = false;
  return atomic_compare_exchange_strong(busy, &expected, true);
}
static inline const char *central_validate(const char *cmd, const cJSON *p) {
  if (p && !cJSON_IsObject(p))
    return "params must be an object";
  const cJSON *v;
  const char *allowed =
      !strcmp(cmd, "ble.scan")      ? "|duration_ms|"
      : !strcmp(cmd, "ble.connect") ? "|address|timeout_ms|"
      : !strcmp(cmd, "ble.read")    ? "|handle|characteristic_uuid|"
      : !strcmp(cmd, "ble.write")
          ? "|handle|characteristic_uuid|value_hex|with_response|"
          : "|handle|";
  const cJSON *field;
  cJSON_ArrayForEach(field, p) {
    if (!field->string || strlen(field->string) > 32)
      return "Unknown BLE parameter";
    char key[36];
    size_t n = strlen(field->string);
    key[0] = '|';
    memcpy(key + 1, field->string, n);
    key[n + 1] = '|';
    key[n + 2] = 0;
    if (!strstr(allowed, key))
      return "Unknown BLE parameter";
    for (const cJSON *next = field->next; next; next = next->next)
      if (next->string && !strcmp(field->string, next->string))
        return "Duplicate BLE parameter";
  }
  if (!strcmp(cmd, "ble.scan")) {
    v = cJSON_GetObjectItemCaseSensitive(p, "duration_ms");
    return v && !central_int(v, 1, 60000)
               ? "duration_ms must be an integer 1-60000"
               : NULL;
  }
  if (!strcmp(cmd, "ble.connect")) {
    uint8_t mac[6];
    v = cJSON_GetObjectItemCaseSensitive(p, "address");
    if (!cJSON_IsString(v) || !central_mac(v->valuestring, mac))
      return "address must be AA:BB:CC:DD:EE:FF";
    v = cJSON_GetObjectItemCaseSensitive(p, "timeout_ms");
    return v && !central_int(v, 100, 60000)
               ? "timeout_ms must be an integer 100-60000"
               : NULL;
  }
  v = cJSON_GetObjectItemCaseSensitive(p, "handle");
  if (!central_int(v, 0, 65534))
    return "handle must be an integer 0-65534";
  if (!strcmp(cmd, "ble.read") || !strcmp(cmd, "ble.write")) {
    v = cJSON_GetObjectItemCaseSensitive(p, "characteristic_uuid");
    if (!cJSON_IsString(v) || !central_uuid(v->valuestring))
      return "characteristic_uuid must be 4, 8 or canonical 36 hex characters";
  }
  if (!strcmp(cmd, "ble.write")) {
    v = cJSON_GetObjectItemCaseSensitive(p, "value_hex");
    if (!cJSON_IsString(v) || !central_hex(v->valuestring))
      return "value_hex must be even-length hex, at most 512 bytes";
    v = cJSON_GetObjectItemCaseSensitive(p, "with_response");
    if (v && !cJSON_IsBool(v))
      return "with_response must be boolean";
  }
  return NULL;
}
