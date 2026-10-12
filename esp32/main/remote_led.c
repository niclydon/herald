// Dedicated pattern worker: one live job; retired jobs complete exactly once.
#include "remote_led.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
bool external_rgb_remote_set(int index,int r,int g,int b,int brightness);
void external_rgb_demo_mode(void);
bool external_rgb_ready(void);
typedef struct {
    char *id; noise_ctrl_session_generation_t generation; cJSON *params;
    int mode,count,speed,duration,brightness; int colors[16][3]; int64_t started;
} pattern_job;
static SemaphoreHandle_t mutex;
static QueueHandle_t retired;
static pattern_job *active;
static cJSON *command_error(const char *code,const char *message) {
    cJSON *r=cJSON_CreateObject(), *e=cJSON_AddObjectToObject(r,"error");
    cJSON_AddBoolToObject(r,"ok",false); cJSON_AddStringToObject(e,"code",code); cJSON_AddStringToObject(e,"message",message); return r;
}
static cJSON *success(const char *status) {
    cJSON *r=cJSON_CreateObject(); cJSON_AddBoolToObject(r,"ok",true);
    if(status) cJSON_AddStringToObject(cJSON_AddObjectToObject(r,"payload"),"status",status);
    return r;
}
static bool integer(cJSON *p,const char *key,int low,int high,int def,bool required,int *out) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(p,key);
    if(!v) { *out=def; return !required; }
    if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<low||v->valuedouble>high||v->valuedouble!=v->valueint) return false;
    *out=v->valueint; return true;
}
static void complete(pattern_job *j,const char *status) {
    noise_ctrl_send_command_result(j->generation,j->id,success(status));
    cJSON_Delete(j->params); free(j->id); free(j);
}
// Called with mutex held. The queue never discards a job or its completion.
static bool supersede(void) {
    if(active && xQueueSend(retired,&active,0)!=pdTRUE) return false;
    active=NULL; return true;
}
void remote_led_poll(void) {
    pattern_job *old;
    while(xQueueReceive(retired,&old,0)==pdTRUE) complete(old,"superseded");
    pattern_job *done=NULL;
    if(xSemaphoreTake(mutex,0)!=pdTRUE) return;
    if(active) {
        int64_t elapsed=(esp_timer_get_time()-active->started)/1000;
        if(active->duration && elapsed>=active->duration) { done=active; active=NULL; }
        else {
            int step=(int)((elapsed/active->speed)%active->count);
            float fraction=(elapsed%active->speed)/(float)active->speed;
            for(int led=0;led<2;led++) {
                int rgb[3], index=step;
                if(active->mode==1) index=(step+led)%active->count;
                if(active->mode==3) index=(int)(((elapsed/active->speed)*1103515245ULL+led*12345ULL)%active->count);
                for(int k=0;k<3;k++) {
                    rgb[k]=active->colors[index][k];
                    if(active->mode==2) rgb[k]=(int)(rgb[k]*(1-fraction)+active->colors[(step+1)%active->count][k]*fraction);
                    if(active->mode==0 && led!=(elapsed/active->speed)%2) rgb[k]=0;
                    if(active->mode==1 && active->count==1 && led!=(elapsed/active->speed)%2) rgb[k]=0;
                }
                int bright=active->brightness;
                if(active->mode==3) bright=(int)(bright*(1.f-fraction));
                if(active->mode==2 && active->count==1) bright=(int)(bright*(1.f-cosf(fraction*6.2831853f))*.5f);
                external_rgb_remote_set(led,rgb[0],rgb[1],rgb[2],bright);
            }
        }
    }
    if(done) for(int i=0;i<2;i++) external_rgb_remote_set(i,0,0,0,0);
    xSemaphoreGive(mutex);
    if(done) complete(done,"duration expired");
}
static void pattern_worker(void *unused) {
    (void)unused;
    for(;;) { remote_led_poll(); vTaskDelay(pdMS_TO_TICKS(20)); }
}
void remote_led_init(void) {
    if(mutex) return;
    if(!external_rgb_ready()) return;
    mutex=xSemaphoreCreateMutex(); retired=xQueueCreate(8,sizeof(pattern_job *));
    if(!mutex||!retired||xTaskCreate(pattern_worker,"Hal.led",4096,NULL,2,NULL)!=pdPASS) {
        if(mutex) vSemaphoreDelete(mutex);
        if(retired) vQueueDelete(retired);
        mutex=NULL; retired=NULL;
    }
}
void remote_led_demo_claim(void) {
    if(!mutex) return;
    // Task context only; never used by the Link command handler.
    if(xSemaphoreTake(mutex,pdMS_TO_TICKS(100))!=pdTRUE) return;
    if(supersede()) external_rgb_demo_mode();
    xSemaphoreGive(mutex);
}
cJSON *remote_led_command(const char *cmd,cJSON *params,const char *id,noise_ctrl_session_generation_t gen) {
    if(!mutex) return command_error("unavailable","LED worker unavailable");
    int index=0,r=0,g=0,b=0,brightness=100,speed=300,duration=0;
    bool pattern=!strcmp(cmd,"led.pattern"), set=!strcmp(cmd,"led.set");
    if(!pattern&&!set&&strcmp(cmd,"led.clear")) return command_error("unsupported","Unknown LED command");
    if(!pattern&&!set && params && cJSON_GetArraySize(params)) return command_error("invalid_params","led.clear takes no parameters");
    if(params && !cJSON_IsObject(params)) return command_error("invalid_params","Parameters must be an object");
    for(cJSON *v=params?params->child:NULL;v;v=v->next) {
        const char *key=v->string?v->string:"";
        bool allowed=!strcmp(key,"brightness") || (set && (!strcmp(key,"index")||!strcmp(key,"r")||!strcmp(key,"g")||!strcmp(key,"b"))) ||
            (pattern && (!strcmp(key,"pattern")||!strcmp(key,"colors")||!strcmp(key,"speed_ms")||!strcmp(key,"duration_ms")));
        if(!allowed) return command_error("invalid_params","Unknown LED parameter");
    }
    if(!integer(params,"brightness",0,100,100,false,&brightness)) return command_error("invalid_params","brightness must be 0-100");
    if(set && (!integer(params,"index",0,1,0,true,&index)||!integer(params,"r",0,255,0,true,&r)||!integer(params,"g",0,255,0,true,&g)||!integer(params,"b",0,255,0,true,&b)))
        return command_error("invalid_params","index 0-1 and r/g/b 0-255 are required integers");
    pattern_job *j=NULL;
    if(pattern) {
        cJSON *p=cJSON_GetObjectItemCaseSensitive(params,"pattern"), *colors=cJSON_GetObjectItemCaseSensitive(params,"colors");
        if(!cJSON_IsString(p)||!p->valuestring||!cJSON_IsArray(colors)||cJSON_GetArraySize(colors)<1||cJSON_GetArraySize(colors)>16||
           !integer(params,"speed_ms",20,60000,300,false,&speed)||!integer(params,"duration_ms",0,86400000,0,false,&duration))
            return command_error("invalid_params","pattern and 1-16 RGB colors required; speed_ms 20-60000; duration_ms 0-86400000");
        int mode=-1; const char *names[]={"chase","alternate","fade","sparkle"};
        for(int i=0;i<4;i++) if(!strcmp(p->valuestring,names[i])) mode=i;
        if(mode<0) return command_error("invalid_params","Unknown pattern");
        if(!id||!gen) return command_error("invalid_session","Missing command session");
        j=calloc(1,sizeof(*j));
        if(!j) return command_error("out_of_memory","Cannot allocate pattern");
        j->count=cJSON_GetArraySize(colors); j->mode=mode; j->speed=speed; j->duration=duration; j->brightness=brightness;
        for(int i=0;i<j->count;i++) {
            cJSON *rgb=cJSON_GetArrayItem(colors,i);
            if(!cJSON_IsArray(rgb)||cJSON_GetArraySize(rgb)!=3) { free(j); return command_error("invalid_params","Each color must be [r,g,b]"); }
            for(int k=0;k<3;k++) {
                cJSON *v=cJSON_GetArrayItem(rgb,k);
                if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>255||v->valuedouble!=v->valueint) { free(j); return command_error("invalid_params","Color channels must be integers 0-255"); }
                j->colors[i][k]=v->valueint;
            }
        }
        j->params=cJSON_Duplicate(params,true); j->id=strdup(id); j->generation=gen;
        if(!j->params||!j->id) { cJSON_Delete(j->params); free(j->id); free(j); return command_error("out_of_memory","Cannot copy pattern"); }
    }
    cJSON *ack=pattern?cJSON_CreateObject():success(NULL);
    if(pattern&&ack) cJSON_AddBoolToObject(ack,"_async",true);
    bool acquired=ack && xSemaphoreTake(mutex,0)==pdTRUE;
    if(!acquired || !supersede()) {
        if(acquired) xSemaphoreGive(mutex);
        if(j) { cJSON_Delete(j->params); free(j->id); free(j); }
        cJSON_Delete(ack); return command_error("busy","LED control busy; retry");
    }
    bool applied=true;
    if(pattern) { j->started=esp_timer_get_time(); active=j; }
    else if(set) applied=external_rgb_remote_set(index,r,g,b,brightness);
    else for(int i=0;i<2;i++) applied=external_rgb_remote_set(i,0,0,0,0)&&applied;
    if(!applied) { cJSON_Delete(ack); ack=command_error("busy","LED hardware busy; retry"); }
    xSemaphoreGive(mutex); return ack;
}
