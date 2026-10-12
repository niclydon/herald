// SPDX-License-Identifier: Apache-2.0
// Optional authenticated HTTPS transport. Commands use the existing Link dispatcher.
#include "herald_lan.h"
#include "cJSON.h"
#include "esp_https_server.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef CONFIG_HOMEHUB_LAN_ADMIN
#define CONFIG_HOMEHUB_LAN_ADMIN 0
#endif
#define JOBS 32
#define BODY_MAX 4096
static SemaphoreHandle_t lock;
static httpd_handle_t server;
typedef struct { char id[65], owner[65]; char *request; cJSON *result; int64_t done; } job_t;
static job_t jobs[JOBS];
static char lease_owner[65];
static int64_t lease_until;
extern const unsigned char cert_start[] asm("_binary_herald_cert_start");
extern const unsigned char cert_end[] asm("_binary_herald_cert_end");
extern const unsigned char key_start[] asm("_binary_herald_key_start");
extern const unsigned char key_end[] asm("_binary_herald_key_end");
static const char *commands[]={"led.set","led.pattern","led.clear","audio.test","audio.play","audio.speak","audio.stop","demo.show","wifi.scan","wifi.status","wifi.reconnect","device.info","device.health","device.diagnostics","device.discover","device.reboot","device.ota","settings.get","settings.set"};
static bool admin_command(const char *s) { return !strcmp(s,"device.ota")||!strcmp(s,"device.reboot")||!strcmp(s,"wifi.reconnect"); }
static bool readonly_command(const char *s) { return !strcmp(s,"device.info")||!strcmp(s,"device.health")||!strcmp(s,"device.diagnostics")||!strcmp(s,"wifi.status")||!strcmp(s,"settings.get")||!strcmp(s,"wifi.scan")||!strcmp(s,"device.discover"); }
static bool identifier(const char *s) {
    if(!s || !*s || strlen(s)>64) return false;
    for(;*s;s++) if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='-'||*s=='_')) return false;
    return true;
}
static esp_err_t reply(httpd_req_t *r,const char *status,cJSON *body) {
    char *text=cJSON_PrintUnformatted(body); cJSON_Delete(body);
    httpd_resp_set_status(r,status); httpd_resp_set_type(r,"application/json");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    esp_err_t rc=httpd_resp_send(r,text?text:"{}",HTTPD_RESP_USE_STRLEN); free(text); return rc;
}
static esp_err_t error(httpd_req_t *r,const char *status,const char *code) {
    cJSON *j=cJSON_CreateObject(); cJSON_AddStringToObject(j,"error",code); return reply(r,status,j);
}
static bool authenticated(httpd_req_t *r) {
    char got[192], expected[192];
    if(httpd_req_get_hdr_value_str(r,"Authorization",got,sizeof(got))!=ESP_OK) return false;
    snprintf(expected,sizeof(expected),"Bearer %s",CONFIG_HOMEHUB_LAN_TOKEN);
    size_t n=strlen(expected); unsigned int diff=(unsigned int)(strlen(got)^n);
    for(size_t i=0;i<n;i++) diff|=(unsigned char)expected[i]^(unsigned char)(i<strlen(got)?got[i]:0);
    return diff==0;
}
static cJSON *job_response(job_t *j) {
    cJSON *r=cJSON_CreateObject(); cJSON_AddStringToObject(r,"id",j->id);
    cJSON_AddStringToObject(r,"state",j->result?"done":"running");
    if(j->result) cJSON_AddItemToObject(r,"result",cJSON_Duplicate(j->result,true));
    return r;
}
void herald_lan_result(const char *id,cJSON *result) {
    if(!lock) { cJSON_Delete(result); return; }
    xSemaphoreTake(lock,portMAX_DELAY);
    for(int i=0;i<JOBS;i++) if(!strcmp(jobs[i].id,id)&&jobs[i].request&&!jobs[i].result) {
        jobs[i].result=result; jobs[i].done=esp_timer_get_time(); result=NULL; break;
    }
    xSemaphoreGive(lock); cJSON_Delete(result);
}
static esp_err_t get(httpd_req_t *r) {
    if(!authenticated(r)) return error(r,"401 Unauthorized","unauthorized");
    if(!strcmp(r->uri,"/v1/describe")) {
        cJSON *out=cJSON_CreateObject(), *list=cJSON_AddArrayToObject(out,"commands");
        cJSON_AddStringToObject(out,"name",CONFIG_HOMEHUB_FRIENDLY_NAME);
        cJSON_AddNumberToObject(out,"protocol",1);
        for(size_t i=0;i<sizeof(commands)/sizeof(*commands);i++)
            if(CONFIG_HOMEHUB_LAN_ADMIN || !admin_command(commands[i])) cJSON_AddItemToArray(list,cJSON_CreateString(commands[i]));
        cJSON_AddBoolToObject(out,"admin_enabled",CONFIG_HOMEHUB_LAN_ADMIN);
        cJSON_AddStringToObject(out,"ownership_scope","LAN clients only; Muse may supersede playback or LEDs");
        return reply(r,"200 OK",out);
    }
    const char *prefix="/v1/jobs/";
    if(strncmp(r->uri,prefix,strlen(prefix))) return error(r,"404 Not Found","not_found");
    const char *id=r->uri+strlen(prefix);
    if(!identifier(id)) return error(r,"400 Bad Request","invalid_id");
    char owner[65]={0};
    httpd_req_get_hdr_value_str(r,"X-Herald-Owner",owner,sizeof(owner));
    cJSON *out=NULL;
    xSemaphoreTake(lock,portMAX_DELAY);
    if(owner[0] && !strcmp(owner,lease_owner) && esp_timer_get_time()<lease_until) lease_until=esp_timer_get_time()+120000000;
    for(int i=0;i<JOBS;i++) if(jobs[i].request&&!strcmp(jobs[i].id,id)) { out=job_response(&jobs[i]); break; }
    xSemaphoreGive(lock);
    return out?reply(r,"200 OK",out):error(r,"404 Not Found","job_not_found");
}
static esp_err_t post(httpd_req_t *r) {
    if(!authenticated(r)) return error(r,"401 Unauthorized","unauthorized");
    if(r->content_len<=0 || r->content_len>BODY_MAX) return error(r,"413 Payload Too Large","body_limit");
    char *body=calloc(1,r->content_len+1); if(!body) return error(r,"503 Service Unavailable","out_of_memory");
    int received=0;
    while(received<r->content_len) { int n=httpd_req_recv(r,body+received,r->content_len-received); if(n<=0) { free(body); return ESP_FAIL; } received+=n; }
    cJSON *in=cJSON_Parse(body);
    if(!cJSON_IsObject(in)) { cJSON_Delete(in); free(body); return error(r,"400 Bad Request","invalid_json"); }
    const char *owner=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(in,"owner"));
    if(!identifier(owner)) { cJSON_Delete(in); free(body); return error(r,"400 Bad Request","invalid_owner"); }
    if(!strcmp(r->uri,"/v1/release")) {
        xSemaphoreTake(lock,portMAX_DELAY);
        bool allowed=!lease_owner[0] || !strcmp(lease_owner,owner) || esp_timer_get_time()>=lease_until;
        if(allowed) { lease_owner[0]=0; lease_until=0; }
        xSemaphoreGive(lock); cJSON_Delete(in); free(body);
        if(!allowed) return error(r,"409 Conflict","owned_by_another_client");
        cJSON *out=cJSON_CreateObject(); cJSON_AddBoolToObject(out,"released",true); return reply(r,"200 OK",out);
    }
    const char *cmd=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(in,"command"));
    const char *id=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(in,"id"));
    cJSON *params=cJSON_GetObjectItemCaseSensitive(in,"params");
    bool known=false;
    for(size_t i=0;cmd&&i<sizeof(commands)/sizeof(*commands);i++) if(!strcmp(cmd,commands[i])) known=true;
    if(!known||!identifier(id)||!cJSON_IsObject(params)) { cJSON_Delete(in); free(body); return error(r,"400 Bad Request","invalid_command_request"); }
    if(admin_command(cmd)&&!CONFIG_HOMEHUB_LAN_ADMIN) { cJSON_Delete(in); free(body); return error(r,"403 Forbidden","admin_disabled"); }
    // Reserved local generation keeps these completions away from the Muse session.
    cJSON *invoke=cJSON_CreateObject();
    cJSON_AddStringToObject(invoke,"method","link.invoke"); cJSON_AddStringToObject(invoke,"id",id);
    cJSON_AddStringToObject(invoke,"command",cmd); cJSON_AddItemToObject(invoke,"params",cJSON_Duplicate(params,true));
    char *wire=cJSON_PrintUnformatted(invoke); cJSON_Delete(invoke);
    if(!wire) { cJSON_Delete(in); free(body); return error(r,"503 Service Unavailable","out_of_memory"); }
    xSemaphoreTake(lock,portMAX_DELAY);
    job_t *slot=NULL; cJSON *existing=NULL; bool mismatch=false;
    for(int i=0;i<JOBS;i++) if(jobs[i].request&&!strcmp(jobs[i].id,id)) {
        mismatch=strcmp(jobs[i].request,body)!=0; existing=job_response(&jobs[i]); break;
    }
    if(existing) {
        xSemaphoreGive(lock); free(wire); cJSON_Delete(in); free(body);
        if(mismatch) { cJSON_Delete(existing); return error(r,"409 Conflict","id_reused_with_different_request"); }
        return reply(r,"200 OK",existing);
    }
    if(!readonly_command(cmd)&&lease_owner[0]&&strcmp(lease_owner,owner)&&esp_timer_get_time()<lease_until) {
        xSemaphoreGive(lock); free(wire); cJSON_Delete(in); free(body); return error(r,"409 Conflict","owned_by_another_client");
    }
    for(int i=0;i<JOBS;i++) if(!jobs[i].request || (jobs[i].result&&esp_timer_get_time()-jobs[i].done>60000000)) { slot=&jobs[i]; break; }
    if(!slot) { xSemaphoreGive(lock); free(wire); cJSON_Delete(in); free(body); return error(r,"503 Service Unavailable","job_table_full_retry_after_60s"); }
    if(!readonly_command(cmd)) { snprintf(lease_owner,sizeof(lease_owner),"%s",owner); lease_until=esp_timer_get_time()+120000000; }
    free(slot->request); cJSON_Delete(slot->result); memset(slot,0,sizeof(*slot));
    snprintf(slot->id,sizeof(slot->id),"%s",id); snprintf(slot->owner,sizeof(slot->owner),"%s",owner); slot->request=body;
    xSemaphoreGive(lock);
    noise_ctrl_lan_invoke(wire); free(wire); cJSON_Delete(in);
    xSemaphoreTake(lock,portMAX_DELAY); cJSON *out=job_response(slot); xSemaphoreGive(lock);
    return reply(r,"202 Accepted",out);
}
void herald_lan_start(void) {
    if(server || strlen(CONFIG_HOMEHUB_LAN_TOKEN)<32) { ESP_LOGW("link.lan","LAN disabled: configure a token of at least 32 characters"); return; }
    lock=xSemaphoreCreateMutex(); if(!lock) return;
    httpd_ssl_config_t config=HTTPD_SSL_CONFIG_DEFAULT();
    config.port_secure=8443; config.httpd.stack_size=8192; config.httpd.max_open_sockets=3;
    config.httpd.lru_purge_enable=true; config.httpd.uri_match_fn=httpd_uri_match_wildcard;
    config.httpd.recv_wait_timeout=5; config.httpd.send_wait_timeout=5;
    config.servercert=cert_start; config.servercert_len=cert_end-cert_start;
    config.prvtkey_pem=key_start; config.prvtkey_len=key_end-key_start;
    if(httpd_ssl_start(&server,&config)!=ESP_OK) { ESP_LOGE("link.lan","HTTPS server failed to start"); return; }
    httpd_uri_t describe={.uri="/v1/describe",.method=HTTP_GET,.handler=get};
    httpd_uri_t status={.uri="/v1/jobs/*",.method=HTTP_GET,.handler=get};
    httpd_uri_t command={.uri="/v1/commands",.method=HTTP_POST,.handler=post};
    httpd_uri_t release={.uri="/v1/release",.method=HTTP_POST,.handler=post};
    httpd_register_uri_handler(server,&describe); httpd_register_uri_handler(server,&status);
    httpd_register_uri_handler(server,&command); httpd_register_uri_handler(server,&release);
    ESP_LOGI("link.lan","Herald authenticated HTTPS ready on port 8443");
}
