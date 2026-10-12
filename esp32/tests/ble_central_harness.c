#include "../main/ble_central.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct sem {
  int mutex, available;
};
static struct sem sems[2];
static int ns, results, deleted, tasks, fail_task, timeout_connect,
    terminations, writes, fail_chunk = -1;
static size_t written;
static void (*task_fn)(void *);
static void *task_arg;
static gap_fn *saved_gap;
static void *saved_arg;
static cJSON *result;
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
  sems[ns].mutex = 1;
  return &sems[ns++];
}
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return &sems[ns++]; }
int xSemaphoreTake(SemaphoreHandle_t s, unsigned ms) {
  (void)ms;
  if (s->mutex)
    return 1;
  int a = s->available;
  s->available = 0;
  return a;
}
int xSemaphoreGive(SemaphoreHandle_t s) {
  s->available = 1;
  return 1;
}
int xTaskCreate(void (*f)(void *), const char *n, unsigned size, void *a,
                unsigned pri, void *h) {
  (void)n;
  (void)size;
  (void)pri;
  (void)h;
  if (fail_task)
    return 0;
  task_fn = f;
  task_arg = a;
  tasks++;
  return 1;
}
void vTaskDelete(void *t) {
  (void)t;
  deleted++;
}
void vTaskDelay(unsigned n) { (void)n; }
void noise_ctrl_send_command_result(noise_ctrl_session_generation_t gen,
                                    const char *id, cJSON *r) {
  assert(gen == 42);
  assert(!strcmp(id, "owned-request"));
  cJSON_Delete(result);
  result = r;
  results++;
}
int ble_hs_synced(void) { return 1; }
int ble_gap_terminate(uint16_t h, uint8_t reason) {
  (void)reason;
  terminations++;
  struct ble_gap_event e = {.type = BLE_GAP_EVENT_DISCONNECT};
  e.disconnect.conn.conn_handle = h;
  saved_gap(&e, saved_arg);
  return 0;
}
int ble_gap_connect(uint8_t own, const ble_addr_t *addr, int32_t ms,
                    const void *p, gap_fn *cb, void *arg) {
  (void)own;
  (void)addr;
  (void)ms;
  (void)p;
  saved_gap = cb;
  saved_arg = arg;
  if (timeout_connect)
    return 0;
  struct ble_gap_event e = {.type = BLE_GAP_EVENT_CONNECT};
  e.connect.conn_handle = 17;
  return cb(&e, arg);
}
int ble_gap_conn_cancel(void) { return 0; }
int ble_gap_disc_cancel(void) { return 0; }
int ble_gap_disc(uint8_t own, int32_t ms, const struct ble_gap_disc_params *p,
                 gap_fn *cb, void *arg) {
  (void)own;
  assert(ms == 10);
  assert(p->itvl == 160 && p->window == 24 && !p->passive);
  struct ble_gap_event e = {.type = BLE_GAP_EVENT_DISC};
  e.disc.addr.val[0] = 1;
  e.disc.rssi = -80;
  cb(&e, arg);
  e.disc.addr.val[0] = 2;
  e.disc.rssi = -35;
  cb(&e, arg);
  e.disc.rssi = -30;
  cb(&e, arg); /* merge scan response, do not duplicate */
  e.type = BLE_GAP_EVENT_DISC_COMPLETE;
  cb(&e, arg);
  return 0;
}
int ble_hs_adv_parse_fields(struct ble_hs_adv_fields *f, const uint8_t *d,
                            uint8_t n) {
  (void)d;
  (void)n;
  memset(f, 0, sizeof(*f));
  static uint8_t name[] = "Fixture", manufacturer[] = {0x01, 0xab};
  static ble_uuid_any_t uuids[] = {{.u.id = 0x180f}};
  f->name = name;
  f->name_len = 7;
  f->mfg_data = manufacturer;
  f->mfg_data_len = 2;
  f->uuids16 = uuids;
  f->num_uuids16 = 1;
  return 0;
}
char *ble_uuid_to_str(const ble_uuid_t *u, char *out) {
  sprintf(out, "%04x", u->id);
  return out;
}
int ble_uuid_from_str(ble_uuid_any_t *u, const char *s) {
  u->u.id = strtoul(s, NULL, 16);
  return 0;
}
int ble_uuid_cmp(const ble_uuid_t *a, const ble_uuid_t *b) {
  return a->id != b->id;
}
int ble_gattc_disc_all_svcs(uint16_t h, svc_fn *cb, void *arg) {
  struct ble_gatt_error e = {0};
  struct ble_gatt_svc s = {
      .start_handle = 1, .end_handle = 5, .uuid.u.id = 0x180f};
  cb(h, &e, &s, arg);
  e.status = BLE_HS_EDONE;
  cb(h, &e, NULL, arg);
  return 0;
}
int ble_gattc_disc_all_chrs(uint16_t h, uint16_t a, uint16_t b, chr_fn *cb,
                            void *arg) {
  (void)a;
  (void)b;
  struct ble_gatt_error e = {0};
  struct ble_gatt_chr c = {
      .val_handle = 3, .properties = 14, .uuid.u.id = 0x2a19};
  cb(h, &e, &c, arg);
  e.status = BLE_HS_EDONE;
  cb(h, &e, NULL, arg);
  return 0;
}
int ble_gattc_read_long(uint16_t h, uint16_t attr, uint16_t offset, attr_fn *cb,
                        void *arg) {
  (void)attr;
  (void)offset;
  uint8_t data[] = {0xab, 0xcd};
  struct os_mbuf om = {data, 2};
  struct ble_gatt_attr a = {.om = &om};
  struct ble_gatt_error e = {0};
  cb(h, &e, &a, arg);
  e.status = BLE_HS_EDONE;
  cb(h, &e, NULL, arg);
  return 0;
}
int ble_gattc_write_no_rsp_flat(uint16_t h, uint16_t a, const void *v,
                                uint16_t n) {
  (void)h;
  (void)a;
  (void)v;
  assert(n <= 20);
  if (writes++ == fail_chunk)
    return 9;
  written += n;
  return 0;
}
int ble_gattc_write_flat(uint16_t h, uint16_t a, const void *v, uint16_t n,
                         attr_fn *cb, void *arg) {
  int rc = ble_gattc_write_no_rsp_flat(h, a, v, n);
  if (rc)
    return rc;
  struct ble_gatt_error e = {0};
  cb(h, &e, NULL, arg);
  return 0;
}
uint16_t ble_att_mtu(uint16_t h) {
  (void)h;
  return 23;
}
int os_mbuf_copydata(const struct os_mbuf *m, int off, int n, void *out) {
  memcpy(out, m->data + off, n);
  return 0;
}
static cJSON *submit(const char *cmd, const char *json) {
  cJSON *p = cJSON_Parse(json);
  char id[] = "owned-request";
  cJSON *r = ble_central_command(cmd, p, id, 42);
  memset(id, 'x', sizeof(id) - 1);
  cJSON_Delete(p);
  return r;
}
static void run(const char *cmd, const char *json) {
  cJSON *r = submit(cmd, json);
  assert(cJSON_IsTrue(cJSON_GetObjectItem(r, "_async")));
  cJSON_Delete(r);
  int old = results;
  task_fn(task_arg);
  assert(results == old + 1);
  assert(!atomic_load(&busy));
}
static void bad(const char *cmd, const char *json) {
  cJSON *r = submit(cmd, json);
  assert(cJSON_IsFalse(cJSON_GetObjectItem(r, "ok")));
  cJSON_Delete(r);
}
int main(void) {
  ble_central_init();
  bad("ble.connect", "{\"address\":\"AA:BB:CC:DD:EE:GG\"}");
  bad("ble.scan", "{\"duration_ms\":60001}");
  bad("ble.scan", "{\"duration_ms\":1.5}");
  bad("ble.read", "{\"handle\":1,\"characteristic_uuid\":\"junk\"}");
  bad("ble.write",
      "{\"handle\":1,\"characteristic_uuid\":\"2a19\",\"value_hex\":\"abc\"}");
  bad("ble.write",
      "{\"handle\":1,\"characteristic_uuid\":\"2a19\",\"value_hex\":\"xx\"}");
  bad("ble.disconnect", "{\"handle\":-1}");
  assert(central_uuid("12345678-1234-1234-1234-123456789abc"));
  assert(central_uuid("12345678"));
  assert(!central_uuid("12345678_1234-1234-1234-123456789abc"));
  bad("ble.scan", "{\"surprise\":1}");
  bad("ble.scan", "{\"duration_ms\":1,\"duration_ms\":2}");
  fail_task = 1;
  bad("ble.scan", "{}");
  assert(!atomic_load(&busy));
  fail_task = 0;
  cJSON *r = submit("ble.scan", "{\"duration_ms\":10}");
  cJSON_Delete(r);
  bad("ble.connect", "{\"address\":\"AA:BB:CC:DD:EE:FF\"}");
  task_fn(task_arg);
  assert(cJSON_IsTrue(cJSON_GetObjectItem(result, "ok")));
  cJSON *scan = cJSON_GetObjectItem(result, "payload");
  assert(cJSON_GetArraySize(scan) == 2);
  cJSON *first = cJSON_GetArrayItem(scan, 0);
  assert(cJSON_GetObjectItem(first, "rssi")->valueint == -30);
  assert(
      !strcmp(cJSON_GetObjectItem(first, "manufacturer_data_hex")->valuestring,
              "01ab"));
  assert(cJSON_GetArraySize(cJSON_GetObjectItem(first, "service_uuids")) == 1);
  run("ble.connect", "{\"address\":\"AA:BB:CC:DD:EE:FF\"}");
  assert(atomic_load(&connection) == 17);
  bad("ble.connect", "{\"address\":\"AA:BB:CC:DD:EE:00\"}");
  run("ble.discover", "{\"handle\":17}");
  assert(cJSON_GetArraySize(cJSON_GetObjectItem(result, "payload")) == 1);
  run("ble.read", "{\"handle\":17,\"characteristic_uuid\":\"2a19\"}");
  assert(!strcmp(
      cJSON_GetObjectItem(cJSON_GetObjectItem(result, "payload"), "value_hex")
          ->valuestring,
      "abcd"));
  run("ble.write",
      "{\"handle\":17,\"characteristic_uuid\":\"2a19\",\"value_hex\":"
      "\"000102030405060708090001020304050607080900010203040506070809\",\"with_"
      "response\":true}");
  assert(writes == 2 && written == 30);
  writes = 0;
  written = 0;
  fail_chunk = 1;
  run("ble.write",
      "{\"handle\":17,\"characteristic_uuid\":\"2a19\",\"value_hex\":"
      "\"000102030405060708090001020304050607080900010203040506070809\"}");
  assert(cJSON_IsFalse(cJSON_GetObjectItem(result, "ok")));
  assert(written == 20);
  run("ble.disconnect", "{\"handle\":17}");
  assert(atomic_load(&connection) == BLE_HS_CONN_HANDLE_NONE);
  timeout_connect = 1;
  run("ble.connect", "{\"address\":\"AA:BB:CC:DD:EE:FF\",\"timeout_ms\":100}");
  assert(cJSON_IsFalse(cJSON_GetObjectItem(result, "ok")));
  int old = results;
  struct ble_gap_event late = {.type = BLE_GAP_EVENT_CONNECT};
  late.connect.conn_handle = 18;
  saved_gap(&late, saved_arg);
  assert(results == old && terminations == 2);
  assert(atomic_load(&connection) == BLE_HS_CONN_HANDLE_NONE);
  assert(tasks == deleted);
  cJSON_Delete(result);
  puts("BLE validation, single-session guard, async ownership, chunk failures "
       "and late callback cleanup passed");
  return 0;
}
