// Dedicated pattern worker: one live job; retired jobs complete exactly once.
#include "sdkconfig.h"
#include "remote_led.h"
#include <stdatomic.h>
#if CONFIG_HOMEHUB_LED_REACTIVE
#include "led_reactive.h"
static bool reactive_ready;
static atomic_bool reactive_active,reactive_stop_requested;
#endif
#include <stdatomic.h>
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
    active=NULL;
#if CONFIG_HOMEHUB_LED_REACTIVE
    reactive_active=false; led_reactive_end();
#endif
    return true;
}
#if CONFIG_HOMEHUB_SHOW
static atomic_bool show_owned,show_running;
static atomic_bool show_cancelled;
static bool show_led_overridden;
static pattern_job show_pattern;
#endif
static void render(pattern_job *p,int64_t elapsed) {
            int step=(int)((elapsed/p->speed)%p->count);
            float fraction=(elapsed%p->speed)/(float)p->speed;
            for(int led=0;led<2;led++) {
                int rgb[3], index=step;
                if(p->mode==1) index=(step+led)%p->count;
                if(p->mode==3) index=(int)(((elapsed/p->speed)*1103515245ULL+led*12345ULL)%p->count);
                for(int k=0;k<3;k++) {
                    rgb[k]=p->colors[index][k];
                    if(p->mode==2) rgb[k]=(int)(rgb[k]*(1-fraction)+p->colors[(step+1)%p->count][k]*fraction);
                    if(p->mode==0 && led!=(elapsed/p->speed)%2) rgb[k]=0;
                    if(p->mode==1 && p->count==1 && led!=(elapsed/p->speed)%2) rgb[k]=0;
                }
                int bright=p->brightness;
                if(p->mode==3) bright=(int)(bright*(1.f-fraction));
                if(p->mode==2 && p->count==1) bright=(int)(bright*(1.f-cosf(fraction*6.2831853f))*.5f);
                external_rgb_remote_set(led,rgb[0],rgb[1],rgb[2],bright);
            }
}
void remote_led_poll(void) {
    pattern_job *old;
    while(xQueueReceive(retired,&old,0)==pdTRUE) complete(old,"superseded");
    pattern_job *done=NULL;
    if(xSemaphoreTake(mutex,0)!=pdTRUE) return;
#if CONFIG_HOMEHUB_LED_REACTIVE
    if(atomic_exchange(&reactive_stop_requested,false) && reactive_active && supersede())
        for(int i=0;i<2;i++) external_rgb_remote_set(i,0,0,0,0);
#endif
#if CONFIG_HOMEHUB_SHOW
    if(show_owned && show_running && !atomic_load(&show_cancelled)) {
        int64_t elapsed=(esp_timer_get_time()-show_pattern.started)/1000;
        if(elapsed>=0) render(&show_pattern,elapsed);
    }
#endif
    #if CONFIG_HOMEHUB_LED_REACTIVE
    if(reactive_active) led_reactive_render(esp_timer_get_time());
#endif
    if(active && active->mode!=4) {
        int64_t elapsed=(esp_timer_get_time()-active->started)/1000;
        if(active->duration && elapsed>=active->duration) { done=active; active=NULL; }
        else {
            render(active,elapsed);
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
    #if CONFIG_HOMEHUB_LED_REACTIVE
    reactive_ready=led_reactive_init();
#endif
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
#if CONFIG_HOMEHUB_SHOW
    if(!show_owned
#if CONFIG_HOMEHUB_LED_REACTIVE
       && !reactive_active
#endif
       && supersede()) external_rgb_demo_mode();
#else
    if(
#if CONFIG_HOMEHUB_LED_REACTIVE
       !reactive_active &&
#endif
       supersede()) external_rgb_demo_mode();
#endif
    xSemaphoreGive(mutex);
}
cJSON *remote_led_command(const char *cmd,cJSON *params,const char *id,noise_ctrl_session_generation_t gen) {
    if(!mutex) return command_error("unavailable","LED worker unavailable");
    int index=0,r=0,g=0,b=0,brightness=100,speed=300,duration=0;
    bool pattern=!strcmp(cmd,"led.pattern"), set=!strcmp(cmd,"led.set");
#if CONFIG_HOMEHUB_LED_REACTIVE
    bool reactive=!strcmp(cmd,"led.reactive");
    reactive_config_t rc;
    if(reactive) {
        const char *bad=reactive_parse(params,&rc);
        if(bad) return command_error("invalid_params",bad);
        if(!reactive_ready) return command_error("unavailable","Audio analysis buffers unavailable");
        pattern=rc.mode!=4;
    }
#else
    bool reactive=false;
#endif
    if(!reactive&&!pattern&&!set&&strcmp(cmd,"led.clear")) return command_error("unsupported","Unknown LED command");
    if(!reactive&&!pattern&&!set && params && cJSON_GetArraySize(params)) return command_error("invalid_params","led.clear takes no parameters");
    if(params && !cJSON_IsObject(params)) return command_error("invalid_params","Parameters must be an object");
    for(cJSON *v=!reactive&&params?params->child:NULL;v;v=v->next) {
        const char *key=v->string?v->string:"";
        bool allowed=!strcmp(key,"brightness") || (set && (!strcmp(key,"index")||!strcmp(key,"r")||!strcmp(key,"g")||!strcmp(key,"b"))) ||
            (pattern && (!strcmp(key,"pattern")||!strcmp(key,"colors")||!strcmp(key,"speed_ms")||!strcmp(key,"duration_ms")));
        if(!allowed) return command_error("invalid_params","Unknown LED parameter");
    }
    if(!integer(params,"brightness",0,100,100,false,&brightness)) return command_error("invalid_params","brightness must be 0-100");
    if(set && (!integer(params,"index",0,1,0,true,&index)||!integer(params,"r",0,255,0,true,&r)||!integer(params,"g",0,255,0,true,&g)||!integer(params,"b",0,255,0,true,&b)))
        return command_error("invalid_params","index 0-1 and r/g/b 0-255 are required integers");
    pattern_job *j=NULL;
    if(pattern && !reactive) {
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
#if CONFIG_HOMEHUB_LED_REACTIVE
    if(reactive && pattern) {
        if(!id||!id[0]||strlen(id)>128||!gen) return command_error("invalid_session","Missing command session");
        j=calloc(1,sizeof(*j));
        if(j) { j->mode=4;j->id=strdup(id);j->generation=gen; }
        if(!j||!j->id) { if(j) free(j);return command_error("out_of_memory","Cannot allocate reactive job"); }
    }
#endif
    cJSON *ack=pattern?cJSON_CreateObject():success(NULL);
    if(pattern&&ack) cJSON_AddBoolToObject(ack,"_async",true);
    bool acquired=ack && xSemaphoreTake(mutex,0)==pdTRUE;
    if(!acquired
#if CONFIG_HOMEHUB_SHOW
       || (show_owned && !show_led_overridden && !reactive
#if CONFIG_HOMEHUB_LED_REACTIVE
           && !reactive_active
#endif
          )
#endif
       || !supersede()) {
        if(acquired) xSemaphoreGive(mutex);
        if(j) { cJSON_Delete(j->params); free(j->id); free(j); }
        cJSON_Delete(ack); return command_error("busy","LED control busy; retry");
    }
    bool applied=true;
#if CONFIG_HOMEHUB_LED_REACTIVE
    if(reactive) {
#if CONFIG_HOMEHUB_SHOW
        show_running=false;show_led_overridden=true;
#endif
    }
    if(reactive && pattern) {
        atomic_store(&reactive_stop_requested,false);reactive_active=true;led_reactive_begin(&rc);
        for(int i=0;i<2;i++) external_rgb_remote_set(i,0,0,0,0);
    }
#endif
    if(pattern) { j->started=esp_timer_get_time(); active=j; }
    else if(set) applied=external_rgb_remote_set(index,r,g,b,brightness);
    else for(int i=0;i<2;i++) applied=external_rgb_remote_set(i,0,0,0,0)&&applied;
    if(!applied) { cJSON_Delete(ack); ack=command_error("busy","LED hardware busy; retry"); }
    xSemaphoreGive(mutex); return ack;
}

#if CONFIG_HOMEHUB_SHOW
bool remote_led_show_claim(void) {
    if(!mutex||xSemaphoreTake(mutex,0)!=pdTRUE) return false;
    bool ok=!show_owned && (
#if CONFIG_HOMEHUB_LED_REACTIVE
        reactive_active ||
#endif
        supersede());
    if(ok) {
#if CONFIG_HOMEHUB_LED_REACTIVE
        show_led_overridden=reactive_active;
#else
        show_led_overridden=false;
#endif
        show_owned=true;show_running=false;atomic_store(&show_cancelled,false); }
    xSemaphoreGive(mutex);return ok;
}
bool remote_led_show_phase(const remote_led_pattern_t *p,int64_t started) {
    if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(100))!=pdTRUE) return false;
    bool ok=show_owned&&!atomic_load(&show_cancelled);
    if(ok && !show_led_overridden) {
        show_pattern.mode=p->mode;show_pattern.count=p->count;show_pattern.speed=p->speed;show_pattern.brightness=p->brightness;
        memcpy(show_pattern.colors,p->colors,sizeof(p->colors));show_pattern.started=started;show_running=true;
    }
    xSemaphoreGive(mutex);return ok;
}
void remote_led_show_hold(void) {
    if(!mutex) return;
    xSemaphoreTake(mutex,portMAX_DELAY);show_running=false;xSemaphoreGive(mutex);
}
bool remote_led_show_clear(void) {
    if(!mutex) return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool ok=show_owned;
    /* Cancellation preserves the existing stop behavior. */
    if(ok && !show_led_overridden && !atomic_load(&show_cancelled)) {
        show_running=false;
        for(int i=0;i<2;i++) ok=external_rgb_remote_set(i,0,0,0,0)&&ok;
    }
    xSemaphoreGive(mutex);
    return ok;
}
void remote_led_show_cancel(void) { atomic_store(&show_cancelled,true); }
void remote_led_show_release(void) {
    atomic_store(&show_cancelled,true);
    atomic_store(&show_running,false);
    atomic_store(&show_owned,false);
}
#endif

#if CONFIG_HOMEHUB_LED_REACTIVE
bool remote_led_reactive_stop(void) {
    bool running=atomic_load(&reactive_active);
    if(running) atomic_store(&reactive_stop_requested,true);
    return running;
}
#endif
