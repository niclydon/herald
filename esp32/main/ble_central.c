/* Herald BLE central: one worker operation, one connection, bounded results.
 * Callback arguments are generation numbers, never pointers to freed jobs.
 * A timed-out callback cannot mutate a newer operation's result. */
#include "ble_central.h"
#include "ble_central_params.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#define MAX_PEERS 16
#define MAX_SERVICES 16
#define MAX_CHARS 64
#define STEP_TIMEOUT 12000
static SemaphoreHandle_t lock, done;
static atomic_bool busy;
static atomic_int connection = BLE_HS_CONN_HANDLE_NONE;
static uintptr_t serial, active;
static int status;
static bool finished;
static struct ble_gatt_svc services[MAX_SERVICES];
static struct {
  struct ble_gatt_chr chr;
  int service;
} chars[MAX_CHARS];
static int service_count, char_count, current_service;
static uint8_t value[CENTRAL_VALUE_MAX];
static size_t value_len;
static bool reading;
typedef struct {
  ble_addr_t addr;
  int rssi;
  char name[32], mfg[63], uuids[8][37];
  int nuuids;
} peer_t;
static peer_t peers[MAX_PEERS];
static int peer_count;
typedef struct {
  char *id;
  char command[20];
  cJSON *params;
  noise_ctrl_session_generation_t gen;
} job_t;
static void take(void) { xSemaphoreTake(lock, portMAX_DELAY); }
static void give(void) { xSemaphoreGive(lock); }
static void complete(int rc) {
  if (!finished) {
    status = rc;
    finished = true;
    xSemaphoreGive(done);
  }
}
static cJSON *command_error(const char *code, const char *message) {
  cJSON *r = cJSON_CreateObject(), *e = cJSON_AddObjectToObject(r, "error");
  cJSON_AddBoolToObject(r, "ok", false);
  cJSON_AddStringToObject(e, "code", code);
  cJSON_AddStringToObject(e, "message", message);
  return r;
}
static cJSON *ok(cJSON *p) {
  cJSON *r = cJSON_CreateObject();
  cJSON_AddBoolToObject(r, "ok", true);
  if (p)
    cJSON_AddItemToObject(r, "payload", p);
  return r;
}
static cJSON *failure(const char *operation, int rc) {
  char msg[128];
  snprintf(msg, sizeof(msg),
           "%s failed (NimBLE status %d); disconnect before retrying after a "
           "timeout",
           operation, rc);
  return command_error(rc == BLE_HS_ETIMEOUT ? "timeout" : "ble_error", msg);
}
static void hex_encode(const uint8_t *src, size_t n, char *out) {
  static const char h[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[i * 2] = h[src[i] >> 4];
    out[i * 2 + 1] = h[src[i] & 15];
  }
  out[n * 2] = 0;
}
static uintptr_t begin(void) {
  take();
  while (xSemaphoreTake(done, 0) == pdTRUE) {
  };
  if (++serial == 0) {
    ++serial;
  }
  active = serial;
  finished = false;
  status = 0;
  uintptr_t n = active;
  give();
  return n;
}
static int wait_step(int rc, int ms) {
  if (!rc && xSemaphoreTake(done, pdMS_TO_TICKS(ms)) != pdTRUE)
    rc = BLE_HS_ETIMEOUT;
  take();
  if (!rc)
    rc = status;
  active = 0;
  give();
  return rc;
}
void ble_central_init(void) {
  lock = xSemaphoreCreateMutex();
  done = xSemaphoreCreateBinary();
}
void ble_central_reset(void) {
  if (!lock) {
    return;
  }
  take();
  atomic_store(&connection, BLE_HS_CONN_HANDLE_NONE);
  if (active)
    complete(BLE_HS_ENOTCONN);
  give();
}
static void add_uuid(peer_t *p, const ble_uuid_t *u) {
  char text[37];
  ble_uuid_to_str(u, text);
  for (int i = 0; i < p->nuuids; i++)
    if (!strcmp(text, p->uuids[i]))
      return;
  if (p->nuuids < 8)
    snprintf(p->uuids[p->nuuids++], 37, "%s", text);
}
static int gap_cb(struct ble_gap_event *e, void *arg) {
  uintptr_t token = (uintptr_t)arg;
  take();
  if (e->type == BLE_GAP_EVENT_DISCONNECT) {
    if (e->disconnect.conn.conn_handle == atomic_load(&connection)) {
      atomic_store(&connection, BLE_HS_CONN_HANDLE_NONE);
      if (active)
        complete(BLE_HS_ENOTCONN);
    }
    give();
    return 0;
  }
  if (token != active || finished) {
    bool orphan = e->type == BLE_GAP_EVENT_CONNECT && !e->connect.status;
    uint16_t h = orphan ? e->connect.conn_handle : 0;
    give();
    if (orphan) {
      ble_gap_terminate(h, BLE_ERR_REM_USER_CONN_TERM);
    }
    return 0;
  }
  if (e->type == BLE_GAP_EVENT_CONNECT) {
    if (!e->connect.status)
      atomic_store(&connection, e->connect.conn_handle);
    complete(e->connect.status);
  } else if (e->type == BLE_GAP_EVENT_DISC_COMPLETE)
    complete(e->disc_complete.reason);
  else if (e->type == BLE_GAP_EVENT_DISC) {
    struct ble_hs_adv_fields f;
    if (!ble_hs_adv_parse_fields(&f, e->disc.data, e->disc.length_data)) {
      int i;
      for (i = 0; i < peer_count; i++)
        if (!memcmp(&peers[i].addr, &e->disc.addr, sizeof(ble_addr_t)))
          break;
      if (i == peer_count && peer_count < MAX_PEERS) {
        memset(&peers[i], 0, sizeof(peers[i]));
        peers[i].addr = e->disc.addr;
        peer_count++;
      }
      if (i < peer_count) {
        peer_t *p = &peers[i];
        p->rssi = e->disc.rssi;
        if (f.name && f.name_len) {
          size_t n = f.name_len < 31 ? f.name_len : 31;
          for (size_t k = 0; k < n; k++)
            p->name[k] = (f.name[k] >= 32 && f.name[k] < 127) ? f.name[k] : '?';
          p->name[n] = 0;
        }
        if (f.mfg_data && f.mfg_data_len)
          hex_encode(f.mfg_data, f.mfg_data_len > 31 ? 31 : f.mfg_data_len,
                     p->mfg);
        for (int k = 0; k < f.num_uuids16; k++)
          add_uuid(p, &f.uuids16[k].u);
        for (int k = 0; k < f.num_uuids32; k++)
          add_uuid(p, &f.uuids32[k].u);
        for (int k = 0; k < f.num_uuids128; k++)
          add_uuid(p, &f.uuids128[k].u);
      }
    }
  }
  give();
  return 0;
}
static int service_cb(uint16_t h, const struct ble_gatt_error *e,
                      const struct ble_gatt_svc *s, void *arg) {
  (void)h;
  take();
  if ((uintptr_t)arg != active || finished) {
    give();
    return BLE_HS_EDONE;
  }
  int stop = 0;
  if (e->status)
    complete(e->status == BLE_HS_EDONE ? 0 : e->status);
  else if (service_count == MAX_SERVICES) {
    complete(BLE_HS_EMSGSIZE);
    stop = BLE_HS_EDONE;
  } else
    services[service_count++] = *s;
  give();
  return stop;
}
static int chr_cb(uint16_t h, const struct ble_gatt_error *e,
                  const struct ble_gatt_chr *c, void *arg) {
  (void)h;
  take();
  if ((uintptr_t)arg != active || finished) {
    give();
    return BLE_HS_EDONE;
  }
  int stop = 0;
  if (e->status)
    complete(e->status == BLE_HS_EDONE ? 0 : e->status);
  else if (char_count == MAX_CHARS) {
    complete(BLE_HS_EMSGSIZE);
    stop = BLE_HS_EDONE;
  } else {
    chars[char_count].chr = *c;
    chars[char_count++].service = current_service;
  }
  give();
  return stop;
}
static int attr_cb(uint16_t h, const struct ble_gatt_error *e,
                   struct ble_gatt_attr *a, void *arg) {
  (void)h;
  take();
  if ((uintptr_t)arg != active || finished) {
    give();
    return BLE_HS_EDONE;
  }
  int stop = 0;
  if (e->status)
    complete(e->status == BLE_HS_EDONE ? 0 : e->status);
  else if (!reading)
    complete(0);
  else {
    size_t n = OS_MBUF_PKTLEN(a->om);
    if (a->offset + n > sizeof(value)) {
      complete(BLE_HS_EMSGSIZE);
      stop = BLE_HS_EDONE;
    } else {
      int rc = os_mbuf_copydata(a->om, 0, n, value + a->offset);
      if (rc) {
        complete(rc);
        stop = BLE_HS_EDONE;
      } else
        value_len = a->offset + n;
    }
  }
  give();
  return stop;
}
static int discover(uint16_t h) {
  take();
  service_count = char_count = 0;
  give();
  uintptr_t t = begin();
  int rc = wait_step(ble_gattc_disc_all_svcs(h, service_cb, (void *)t),
                     STEP_TIMEOUT);
  for (int i = 0; !rc && i < service_count; i++) {
    current_service = i;
    t = begin();
    rc = wait_step(ble_gattc_disc_all_chrs(h, services[i].start_handle,
                                           services[i].end_handle, chr_cb,
                                           (void *)t),
                   STEP_TIMEOUT);
  }
  return rc;
}
static cJSON *discovery_json(void) {
  cJSON *arr = cJSON_CreateArray();
  char u[37];
  for (int i = 0; i < service_count; i++) {
    cJSON *s = cJSON_CreateObject(),
          *cs = cJSON_AddArrayToObject(s, "characteristics");
    ble_uuid_to_str(&services[i].uuid.u, u);
    cJSON_AddStringToObject(s, "service_uuid", u);
    for (int k = 0; k < char_count; k++)
      if (chars[k].service == i) {
        cJSON *c = cJSON_CreateObject(),
              *ps = cJSON_AddArrayToObject(c, "properties");
        ble_uuid_to_str(&chars[k].chr.uuid.u, u);
        cJSON_AddStringToObject(c, "uuid", u);
        const char *names[] = {"read", "write", "write_no_response", "notify",
                               "indicate"};
        int flags[] = {BLE_GATT_CHR_PROP_READ, BLE_GATT_CHR_PROP_WRITE,
                       BLE_GATT_CHR_PROP_WRITE_NO_RSP, BLE_GATT_CHR_PROP_NOTIFY,
                       BLE_GATT_CHR_PROP_INDICATE};
        for (int j = 0; j < 5; j++) {
          if (chars[k].chr.properties & flags[j])
            cJSON_AddItemToArray(ps, cJSON_CreateString(names[j]));
        }
        cJSON_AddItemToArray(cs, c);
      }
    cJSON_AddItemToArray(arr, s);
  }
  return arr;
}
static int compare_peer(const void *a, const void *b) {
  return ((const peer_t *)b)->rssi - ((const peer_t *)a)->rssi;
}
static cJSON *scan_json(void) {
  qsort(peers, peer_count, sizeof(peers[0]), compare_peer);
  cJSON *arr = cJSON_CreateArray();
  for (int i = 0; i < peer_count; i++) {
    peer_t *p = &peers[i];
    cJSON *o = cJSON_CreateObject(),
          *uu = cJSON_AddArrayToObject(o, "service_uuids");
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", p->addr.val[5],
             p->addr.val[4], p->addr.val[3], p->addr.val[2], p->addr.val[1],
             p->addr.val[0]);
    cJSON_AddStringToObject(o, "name", p->name);
    cJSON_AddStringToObject(o, "address", mac);
    cJSON_AddNumberToObject(o, "rssi", p->rssi);
    cJSON_AddStringToObject(o, "manufacturer_data_hex", p->mfg);
    for (int k = 0; k < p->nuuids; k++) {
      cJSON_AddItemToArray(uu, cJSON_CreateString(p->uuids[k]));
    }
    cJSON_AddItemToArray(arr, o);
  }
  return arr;
}
static int number(cJSON *p, const char *key, int def) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(p, key);
  return v ? v->valueint : def;
}
static void worker(void *arg) {
  job_t *j = arg;
  ESP_LOGI("link.ble.central", "%s started; Link connected=%d", j->command,
           noise_ctrl_is_connected());
  cJSON *out = NULL;
  int rc = 0;
  uint16_t h = atomic_load(&connection);
  if (!ble_hs_synced()) {
    out = command_error("unavailable", "NimBLE host is not synchronized");
    goto finish;
  }
  if (!strcmp(j->command, "ble.scan")) {
    take();
    peer_count = 0;
    give();
    int ms = number(j->params, "duration_ms", 10000);
    struct ble_gap_disc_params p = {.itvl = 160,
                                    .window = 24,
                                    .passive = 0,
                                    .filter_duplicates =
                                        0}; /* 100 ms / 15 ms */
    uintptr_t t = begin();
    int started = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, ms, &p, gap_cb, (void *)t);
    rc = wait_step(started, ms + 2000);
    if (rc && !started)
      ble_gap_disc_cancel();
    if (!rc)
      out = ok(scan_json());
  } else if (!strcmp(j->command, "ble.connect")) {
    ble_addr_t addr = {.type = BLE_ADDR_PUBLIC};
    central_mac(
        cJSON_GetObjectItemCaseSensitive(j->params, "address")->valuestring,
        addr.val);
    for (int i = 0; i < peer_count; i++)
      if (!memcmp(addr.val, peers[i].addr.val, 6)) {
        addr.type = peers[i].addr.type;
        break;
      }
    take();
    service_count = char_count = 0;
    give();
    int ms = number(j->params, "timeout_ms", 10000);
    uintptr_t t = begin();
    int started = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &addr, ms, NULL, gap_cb,
                                  (void *)t);
    rc = wait_step(started, ms + 2000);
    if (rc && !started)
      ble_gap_conn_cancel();
    if (!rc) {
      cJSON *p = cJSON_CreateObject();
      cJSON_AddNumberToObject(p, "handle", atomic_load(&connection));
      out = ok(p);
    }
  } else if (h == BLE_HS_CONN_HANDLE_NONE ||
             h != number(j->params, "handle", -1))
    out = command_error("invalid_handle",
                        "No open central connection with this handle");
  else if (!strcmp(j->command, "ble.disconnect")) {
    uintptr_t t = begin();
    (void)t;
    rc = ble_gap_terminate(h, BLE_ERR_REM_USER_CONN_TERM);
    if (!rc) {
      rc = wait_step(0, STEP_TIMEOUT);
      if (rc == BLE_HS_ENOTCONN)
        rc = 0;
    } else
      (void)wait_step(rc, 0);
    if (!rc) {
      service_count = char_count = 0;
      out = ok(NULL);
    }
  } else {
    /* Refresh GATT each time: no stale attribute cache after Service Changed.
     */
    rc = discover(h);
    if (!rc && !strcmp(j->command, "ble.discover"))
      out = ok(discovery_json());
    else if (!rc) {
      ble_uuid_any_t wanted;
      ble_uuid_from_str(&wanted, cJSON_GetObjectItemCaseSensitive(
                                     j->params, "characteristic_uuid")
                                     ->valuestring);
      int found = -1;
      for (int i = 0; i < char_count; i++)
        if (!ble_uuid_cmp(&wanted.u, &chars[i].chr.uuid.u)) {
          if (found != -1) {
            found = -2;
            break;
          }
          found = i;
        }
      if (found < 0)
        out = command_error(found == -2 ? "ambiguous_uuid" : "not_found",
                            found == -2
                                ? "Multiple characteristics share this UUID"
                                : "Characteristic not found");
      else if (!strcmp(j->command, "ble.read")) {
        reading = true;
        value_len = 0;
        uintptr_t t = begin();
        rc = wait_step(ble_gattc_read_long(h, chars[found].chr.val_handle, 0,
                                           attr_cb, (void *)t),
                       STEP_TIMEOUT);
        if (!rc) {
          char text[CENTRAL_VALUE_MAX * 2 + 1];
          hex_encode(value, value_len, text);
          cJSON *p = cJSON_CreateObject();
          cJSON_AddStringToObject(p, "value_hex", text);
          out = ok(p);
        }
      } else {
        const char *text =
            cJSON_GetObjectItemCaseSensitive(j->params, "value_hex")
                ->valuestring;
        size_t n = strlen(text) / 2;
        for (size_t i = 0; i < n; i++)
          value[i] = (central_hex_digit(text[i * 2]) << 4) |
                     central_hex_digit(text[i * 2 + 1]);
        bool response = cJSON_IsTrue(
            cJSON_GetObjectItemCaseSensitive(j->params, "with_response"));
        reading = false;
        uint16_t mtu = ble_att_mtu(h);
        size_t chunk = mtu >= 23 ? mtu - 3 : 20;
        size_t offset = 0;
        unsigned index = 0;
        if (!(chars[found].chr.properties &
              (response ? BLE_GATT_CHR_PROP_WRITE
                        : BLE_GATT_CHR_PROP_WRITE_NO_RSP)))
          out = command_error(
              "not_writable",
              "Characteristic does not support the requested write mode");
        else
          do {
            size_t size = n - offset;
            if (size > chunk)
              size = chunk;
            if (response) {
              uintptr_t t = begin();
              rc = wait_step(ble_gattc_write_flat(
                                 h, chars[found].chr.val_handle, value + offset,
                                 size, attr_cb, (void *)t),
                             STEP_TIMEOUT);
            } else {
              rc = ble_gattc_write_no_rsp_flat(h, chars[found].chr.val_handle,
                                               value + offset, size);
              if (!rc)
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            if (rc) {
              char msg[150];
              snprintf(msg, sizeof(msg),
                       "write chunk %u at byte %u failed (status %d); %u bytes "
                       "submitted; remote state may be partial",
                       index, (unsigned)offset, rc, (unsigned)offset);
              out = command_error("write_failed", msg);
              break;
            }
            offset += size;
            index++;
          } while (offset < n);
        if (!out && !rc)
          out = ok(NULL);
      }
    }
  }
  if (!out)
    out = failure(j->command, rc);
  if (rc == BLE_HS_ETIMEOUT && strcmp(j->command, "ble.scan") &&
      atomic_load(&connection) != BLE_HS_CONN_HANDLE_NONE)
    ble_gap_terminate(atomic_load(&connection), BLE_ERR_REM_USER_CONN_TERM);
finish:
  ESP_LOGI("link.ble.central", "%s completed; Link connected=%d", j->command,
           noise_ctrl_is_connected());
  noise_ctrl_send_command_result(j->gen, j->id, out);
  cJSON_Delete(j->params);
  free(j->id);
  free(j);
  atomic_store(&busy, false);
  vTaskDelete(NULL);
}
cJSON *ble_central_command(const char *cmd, cJSON *p, const char *id,
                           noise_ctrl_session_generation_t gen) {
  const char *bad = central_validate(cmd, p);
  if (bad)
    return command_error("invalid_params", bad);
  if (!lock || !done)
    return command_error("unavailable", "BLE central initialization failed");
  if (!id || !id[0] || strlen(id) > 128)
    return command_error("invalid_params", "Invalid request ID");
  if (!central_reserve(&busy))
    return command_error("busy", "Another BLE operation is active; wait, then "
                                 "ble.disconnect before a new connection");
  if (!strcmp(cmd, "ble.connect") &&
      atomic_load(&connection) != BLE_HS_CONN_HANDLE_NONE) {
    atomic_store(&busy, false);
    return command_error(
        "already_connected",
        "Call ble.disconnect before opening another connection");
  }
  job_t *j = calloc(1, sizeof(*j));
  if (j) {
    j->id = strdup(id);
    j->params = p ? cJSON_Duplicate(p, true) : cJSON_CreateObject();
    j->gen = gen;
    snprintf(j->command, sizeof(j->command), "%s", cmd);
  }
  if (!j || !j->id || !j->params) {
    if (j) {
      free(j->id);
      cJSON_Delete(j->params);
      free(j);
    }
    atomic_store(&busy, false);
    return command_error("out_of_memory", "Cannot copy BLE request");
  }
  cJSON *accepted = cJSON_CreateObject();
  if (accepted)
    cJSON_AddBoolToObject(accepted, "_async", true);
  if (!accepted ||
      xTaskCreate(worker, "ble_central", 6144, j, 3, NULL) != pdPASS) {
    cJSON_Delete(accepted);
    free(j->id);
    cJSON_Delete(j->params);
    free(j);
    atomic_store(&busy, false);
    return command_error("out_of_memory", "Cannot start BLE worker");
  }
  return accepted;
}
