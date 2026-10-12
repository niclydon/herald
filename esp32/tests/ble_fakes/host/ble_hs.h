#pragma once
#include <stddef.h>
#include <stdint.h>
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_HS_ETIMEOUT 13
#define BLE_HS_ENOTCONN 7
#define BLE_HS_EDONE 14
#define BLE_HS_EMSGSIZE 4
#define BLE_ERR_REM_USER_CONN_TERM 19
#define BLE_OWN_ADDR_PUBLIC 0
#define BLE_ADDR_PUBLIC 0
#define BLE_GAP_EVENT_DISCONNECT 1
#define BLE_GAP_EVENT_CONNECT 2
#define BLE_GAP_EVENT_DISC_COMPLETE 3
#define BLE_GAP_EVENT_DISC 4
#define BLE_GATT_CHR_PROP_READ 2
#define BLE_GATT_CHR_PROP_WRITE 8
#define BLE_GATT_CHR_PROP_WRITE_NO_RSP 4
#define BLE_GATT_CHR_PROP_NOTIFY 16
#define BLE_GATT_CHR_PROP_INDICATE 32
typedef struct {
  uint8_t type, val[6];
} ble_addr_t;
typedef struct {
  uint8_t type;
  uint32_t id;
} ble_uuid_t;
typedef union {
  ble_uuid_t u;
} ble_uuid_any_t;
struct ble_gatt_svc {
  uint16_t start_handle, end_handle;
  ble_uuid_any_t uuid;
};
struct ble_gatt_chr {
  uint16_t def_handle, val_handle;
  uint8_t properties;
  ble_uuid_any_t uuid;
};
struct os_mbuf {
  uint8_t *data;
  uint16_t len;
};
#define OS_MBUF_PKTLEN(m) ((m)->len)
struct ble_gatt_attr {
  uint16_t handle, offset;
  struct os_mbuf *om;
};
struct ble_gatt_error {
  uint16_t status, att_handle;
};
struct ble_gap_event {
  int type;
  struct {
    struct {
      uint16_t conn_handle;
    } conn;
  } disconnect;
  struct {
    int status;
    uint16_t conn_handle;
  } connect;
  struct {
    int reason;
  } disc_complete;
  struct {
    ble_addr_t addr;
    int8_t rssi;
    uint8_t *data;
    uint8_t length_data;
  } disc;
};
struct ble_gap_disc_params {
  uint16_t itvl, window;
  uint8_t passive, filter_duplicates;
};
struct ble_hs_adv_fields {
  uint8_t *name, *mfg_data;
  uint8_t name_len, mfg_data_len, num_uuids16, num_uuids32, num_uuids128;
  ble_uuid_any_t *uuids16, *uuids32, *uuids128;
};
typedef int gap_fn(struct ble_gap_event *, void *);
typedef int svc_fn(uint16_t, const struct ble_gatt_error *,
                   const struct ble_gatt_svc *, void *);
typedef int chr_fn(uint16_t, const struct ble_gatt_error *,
                   const struct ble_gatt_chr *, void *);
typedef int attr_fn(uint16_t, const struct ble_gatt_error *,
                    struct ble_gatt_attr *, void *);
int ble_hs_synced(void);
int ble_gap_terminate(uint16_t, uint8_t);
int ble_gap_disc(uint8_t, int32_t, const struct ble_gap_disc_params *, gap_fn *,
                 void *);
int ble_gap_disc_cancel(void);
int ble_gap_connect(uint8_t, const ble_addr_t *, int32_t, const void *,
                    gap_fn *, void *);
int ble_gap_conn_cancel(void);
int ble_hs_adv_parse_fields(struct ble_hs_adv_fields *, const uint8_t *,
                            uint8_t);
char *ble_uuid_to_str(const ble_uuid_t *, char *);
int ble_uuid_from_str(ble_uuid_any_t *, const char *);
int ble_uuid_cmp(const ble_uuid_t *, const ble_uuid_t *);
int ble_gattc_disc_all_svcs(uint16_t, svc_fn *, void *);
int ble_gattc_disc_all_chrs(uint16_t, uint16_t, uint16_t, chr_fn *, void *);
int ble_gattc_read_long(uint16_t, uint16_t, uint16_t, attr_fn *, void *);
int ble_gattc_write_flat(uint16_t, uint16_t, const void *, uint16_t, attr_fn *,
                         void *);
int ble_gattc_write_no_rsp_flat(uint16_t, uint16_t, const void *, uint16_t);
uint16_t ble_att_mtu(uint16_t);
int os_mbuf_copydata(const struct os_mbuf *, int, int, void *);
