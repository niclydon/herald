#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t; typedef void *httpd_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define HTTPD_RESP_USE_STRLEN -1
#define HTTP_GET 0
#define HTTP_POST 1
#define portMAX_DELAY 0xffffffff
#define CONFIG_HOMEHUB_LAN_TOKEN "synthetic-test-token-not-a-real-key"
#define CONFIG_HOMEHUB_FRIENDLY_NAME "Herald"
typedef struct { const char *uri,*body,*auth,*owner; int content_len,offset; char *response; const char *status; } httpd_req_t;
typedef struct { const char *uri;int method;esp_err_t (*handler)(httpd_req_t *); } httpd_uri_t;
typedef struct { int port_secure;struct {int stack_size,max_open_sockets,lru_purge_enable,recv_wait_timeout,send_wait_timeout; void *uri_match_fn;} httpd;const unsigned char *servercert,*prvtkey_pem;size_t servercert_len,prvtkey_len; } httpd_ssl_config_t;
#define HTTPD_SSL_CONFIG_DEFAULT() {0}
#define httpd_uri_match_wildcard ((void *)1)
int httpd_req_get_hdr_value_str(httpd_req_t *,const char *,char *,size_t);
int httpd_resp_set_status(httpd_req_t *,const char *);
int httpd_resp_set_type(httpd_req_t *,const char *);
int httpd_resp_set_hdr(httpd_req_t *,const char *,const char *);
int httpd_resp_send(httpd_req_t *,const char *,int);
int httpd_req_recv(httpd_req_t *,char *,size_t);
int httpd_ssl_start(httpd_handle_t *,httpd_ssl_config_t *);
int httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t *);
