#include "remote_device.h"
#include "cJSON.h"
#include <math.h>
#include "wifi_mgr.h"
#include "config_store.h"
#include "diagnostic_log.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
static atomic_bool network_busy;
static atomic_bool reboot_pending;
static const char *TAG="link." CONFIG_HOMEHUB_SHORT_NAME;
static atomic_int saved_volume=75, saved_brightness=100;
int remote_default_volume(void) { return atomic_load(&saved_volume); }
int remote_default_brightness(void) { return atomic_load(&saved_brightness); }
void remote_preferences_init(void) {
    char buf[128];
    if(config_get_str("herald_prefs",buf,sizeof(buf))) {
        cJSON *p=cJSON_Parse(buf), *v=cJSON_GetObjectItem(p,"volume"), *b=cJSON_GetObjectItem(p,"brightness");
        if(cJSON_IsNumber(v)&&v->valueint>=0&&v->valueint<=100) atomic_store(&saved_volume,v->valueint);
        if(cJSON_IsNumber(b)&&b->valueint>=0&&b->valueint<=100) atomic_store(&saved_brightness,b->valueint);
        cJSON_Delete(p);
    }
    ESP_LOGI(TAG,"%s preferences loaded: volume=%d brightness=%d",CONFIG_HOMEHUB_FRIENDLY_NAME,remote_default_volume(),remote_default_brightness());
}
typedef struct { char *id; noise_ctrl_session_generation_t gen; bool reconnect; int kind; cJSON *params; } network_job;
static cJSON *command_error(const char *code,const char *msg) {
    cJSON *r=cJSON_CreateObject(), *e=cJSON_AddObjectToObject(r,"error");
    cJSON_AddBoolToObject(r,"ok",false); cJSON_AddStringToObject(e,"code",code); cJSON_AddStringToObject(e,"message",msg); return r;
}
static cJSON *success(cJSON *payload) {
    cJSON *r=cJSON_CreateObject(); cJSON_AddBoolToObject(r,"ok",true);
    if(payload) cJSON_AddItemToObject(r,"payload",payload);
    return r;
}
static cJSON *preferences(void) {
    cJSON *p=cJSON_CreateObject(); cJSON_AddNumberToObject(p,"volume",remote_default_volume()); cJSON_AddNumberToObject(p,"brightness",remote_default_brightness()); return p;
}
static cJSON *diagnostics(void) {
    cJSON *p=cJSON_CreateObject(); const esp_partition_t *part=esp_ota_get_running_partition(); esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;
    if(part) esp_ota_get_state_partition(part,&state);
    cJSON_AddStringToObject(p,"name",CONFIG_HOMEHUB_FRIENDLY_NAME); cJSON_AddStringToObject(p,"short_name",CONFIG_HOMEHUB_SHORT_NAME);
    cJSON_AddNumberToObject(p,"reset_reason",esp_reset_reason()); cJSON_AddNumberToObject(p,"uptime_s",esp_timer_get_time()/1000000);
    cJSON_AddNumberToObject(p,"minimum_free_heap",esp_get_minimum_free_heap_size()); cJSON_AddNumberToObject(p,"ota_state",state);
    cJSON_AddStringToObject(p,"running_partition",part?part->label:"");
    cJSON_AddBoolToObject(p,"ota_rollback_enabled",CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE);
    diagnostic_log_snapshot_t snap={0}; char tail[2049]={0}; size_t used=0;
    if(diagnostic_log_snapshot(&snap)) {
        char *cursor=snap.data;
        while(cursor && *cursor) {
            char *end=strchr(cursor,'\n'); if(end) *end=0;
            if((strstr(cursor,"E (")||strstr(cursor,"W (")) && !strstr(cursor,"token") && !strstr(cursor,"Token") && !strstr(cursor,"password") && !strstr(cursor,"Bearer") && !strstr(cursor,"mgst_") && !strstr(cursor,"Authorization")) {
                size_t n=strlen(cursor); if(n>2047) n=2047;
                if(used+n+1>2048) { used=0; tail[0]=0; }
                memcpy(tail+used,cursor,n); used+=n; tail[used++]='\n'; tail[used]=0;
            }
            cursor=end?end+1:NULL;
        }
        cJSON_AddNumberToObject(p,"dropped_log_lines",snap.dropped_lines+snap.overwritten_lines);
        diagnostic_log_snapshot_free(&snap);
    }
    cJSON_AddStringToObject(p,"recent_errors",tail); return p;
}
static cJSON *wifi_status(void) {
    cJSON *p=cJSON_CreateObject(); wifi_ap_record_t ap={0}; esp_netif_ip_info_t ip={0}; char address[16]="";
    bool connected=esp_wifi_sta_get_ap_info(&ap)==ESP_OK;
    esp_netif_t *netif=wifi_mgr_get_netif();
    if(netif && esp_netif_get_ip_info(netif,&ip)==ESP_OK && connected) snprintf(address,sizeof(address),IPSTR,IP2STR(&ip.ip));
    cJSON_AddStringToObject(p,"ssid",connected?(char *)ap.ssid:""); cJSON_AddStringToObject(p,"ip",address);
    cJSON_AddNumberToObject(p,"rssi",connected?ap.rssi:0);
    cJSON_AddNumberToObject(p,"reconnects",wifi_mgr_reconnect_count());
    cJSON_AddNumberToObject(p,"uptime_s",esp_timer_get_time()/1000000);
    cJSON_AddStringToObject(p,"name",CONFIG_HOMEHUB_FRIENDLY_NAME); cJSON_AddStringToObject(p,"short_name",CONFIG_HOMEHUB_SHORT_NAME);
    return p;
}
static void network_worker(void *arg) {
    network_job *j=arg; cJSON *result;
    if(j->kind==2) result=success(diagnostics());
    else if(j->kind==3) {
        cJSON *p=preferences(), *v=cJSON_GetObjectItem(j->params,"volume"), *b=cJSON_GetObjectItem(j->params,"brightness");
        if(v) cJSON_SetNumberValue(cJSON_GetObjectItem(p,"volume"),v->valueint);
        if(b) cJSON_SetNumberValue(cJSON_GetObjectItem(p,"brightness"),b->valueint);
        char *value=cJSON_PrintUnformatted(p);
        if(value && config_set_str("herald_prefs",value)) {
            atomic_store(&saved_volume,cJSON_GetObjectItem(p,"volume")->valueint); atomic_store(&saved_brightness,cJSON_GetObjectItem(p,"brightness")->valueint);
            result=success(p);
        } else { cJSON_Delete(p); result=command_error("storage_failed","Preferences were not saved"); }
        free(value);
    }
    else if(j->reconnect) {
        // Give Link time to process the async acceptance before dropping STA.
        vTaskDelay(pdMS_TO_TICKS(500));
        char ssid[33]={0},password[65]={0};
        bool have=config_get_str("ssid",ssid,sizeof(ssid)); config_get_str("password",password,sizeof(password));
        if(CONFIG_HOMEHUB_WIFI_SSID[0]) { snprintf(ssid,sizeof(ssid),"%s",CONFIG_HOMEHUB_WIFI_SSID); snprintf(password,sizeof(password),"%s",CONFIG_HOMEHUB_WIFI_PASSWORD); have=true; }
        if(!have||!ssid[0]) result=command_error("not_configured","No stored Wi-Fi network");
        else {
            wifi_mgr_disconnect();
            bool ok=wifi_mgr_connect(ssid,password,30000);
            result=ok?success(wifi_status()):command_error("connect_failed","Stored network did not reconnect within 30 seconds");
        }
        memset(password,0,sizeof(password));
    } else {
        wifi_scan_entry_t entries[32];
        int n=wifi_mgr_scan(entries,32,0,NULL);
        if(n<0) result=command_error("scan_failed","Wi-Fi radio busy or scan failed");
        else {
            cJSON *array=cJSON_CreateArray();
            for(int i=0;i<n;i++) { cJSON *ap=cJSON_CreateObject(); cJSON_AddStringToObject(ap,"ssid",entries[i].ssid);
                cJSON_AddNumberToObject(ap,"rssi",entries[i].rssi); cJSON_AddNumberToObject(ap,"channel",entries[i].channel);
                cJSON_AddNumberToObject(ap,"auth",entries[i].auth_mode); cJSON_AddItemToArray(array,ap); }
            result=success(array);
        }
    }
    // Exactly one attempt; transport deliberately rejects stale sessions.
    noise_ctrl_send_command_result(j->gen,j->id,result);
    cJSON_Delete(j->params); free(j->id); free(j); atomic_store(&network_busy,false); vTaskDelete(NULL);
}
static void reboot_worker(void *unused) { (void)unused; vTaskDelay(pdMS_TO_TICKS(500)); esp_restart(); vTaskDelete(NULL); }
cJSON *remote_device_command(const char *cmd,cJSON *params,const char *id,noise_ctrl_session_generation_t gen) {
    (void)TAG;
    bool settings=!strcmp(cmd,"settings.set");
    if(!settings && params && (!cJSON_IsObject(params)||cJSON_GetArraySize(params)!=0)) return command_error("invalid_params","This command takes no parameters");
    if(!strcmp(cmd,"settings.get")) return success(preferences());
    if(settings) {
        if(!cJSON_IsObject(params)||!cJSON_GetArraySize(params)) return command_error("invalid_params","Provide volume or brightness");
        for(cJSON *v=params->child;v;v=v->next) {
            if(!v->string||(strcmp(v->string,"volume")&&strcmp(v->string,"brightness"))||!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>100||v->valuedouble!=v->valueint)
                return command_error("invalid_params","volume and brightness must be integers 0-100");
        }
    }
    if(!strcmp(cmd,"wifi.status")) return success(wifi_status());
    if(!strcmp(cmd,"device.info")) {
        cJSON *p=cJSON_CreateObject(); esp_chip_info_t chip; esp_chip_info(&chip); uint32_t flash=0; esp_flash_get_size(NULL,&flash);
        cJSON_AddStringToObject(p,"firmware_version",esp_app_get_description()->version);
        cJSON_AddStringToObject(p,"idf_version",esp_get_idf_version()); cJSON_AddStringToObject(p,"chip",CONFIG_IDF_TARGET);
        cJSON_AddNumberToObject(p,"flash_mb",flash/(1024*1024)); cJSON_AddNumberToObject(p,"psram_mb",esp_psram_get_size()/(1024*1024));
        cJSON_AddNumberToObject(p,"free_heap",esp_get_free_heap_size()); cJSON_AddNumberToObject(p,"uptime_s",esp_timer_get_time()/1000000);
        cJSON_AddStringToObject(p,"name",CONFIG_HOMEHUB_FRIENDLY_NAME); cJSON_AddStringToObject(p,"short_name",CONFIG_HOMEHUB_SHORT_NAME);
        return success(p);
    }
    if(!strcmp(cmd,"device.reboot")) {
        bool expected=false;
        if(!atomic_compare_exchange_strong(&reboot_pending,&expected,true)) return command_error("busy","Reboot already pending");
        cJSON *ack=success(NULL);
        if(!ack||xTaskCreate(reboot_worker,"Hal.reboot",3072,NULL,2,NULL)!=pdPASS) { cJSON_Delete(ack); atomic_store(&reboot_pending,false); return command_error("out_of_memory","Cannot schedule reboot"); }
        return ack;
    }
    if(strcmp(cmd,"wifi.scan")&&strcmp(cmd,"wifi.reconnect")&&strcmp(cmd,"device.diagnostics")&&!settings) return command_error("unsupported","Unknown management command");
    if(!id||!gen) return command_error("invalid_session","Missing session");
    bool expected=false;
    if(!atomic_compare_exchange_strong(&network_busy,&expected,true)) return command_error("busy","Network operation in progress");
    network_job *j=calloc(1,sizeof(*j)); cJSON *ack=cJSON_CreateObject();
    if(j) { j->id=strdup(id); j->gen=gen; j->reconnect=!strcmp(cmd,"wifi.reconnect"); j->kind=settings?3:!strcmp(cmd,"device.diagnostics")?2:0; j->params=settings?cJSON_Duplicate(params,true):NULL; }
    if(ack) { cJSON_AddBoolToObject(ack,"_async",true); cJSON_AddBoolToObject(ack,"ok",true); }
    if(!j||!j->id||(settings&&!j->params)||!ack||xTaskCreate(network_worker,"Hal.wifi",6144,j,2,NULL)!=pdPASS) {
        if(j) { cJSON_Delete(j->params); free(j->id); free(j); } cJSON_Delete(ack); atomic_store(&network_busy,false);
        return command_error("out_of_memory","Cannot start network worker");
    }
    return ack;
}
