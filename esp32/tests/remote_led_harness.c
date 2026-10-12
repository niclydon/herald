#include "remote_led.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int tasks,done,seen[256],mutex; static int64_t now;
static int pixels[2][3];
static void *queue[8]; static unsigned head,count;
void *xSemaphoreCreateMutex(void) { return &mutex; }
int xSemaphoreTake(void *m,unsigned delay) { (void)m;(void)delay;return 1; }
void xSemaphoreGive(void *m) { (void)m; }
void vSemaphoreDelete(void *m) { (void)m; }
void *xQueueCreate(unsigned n,unsigned size) { assert(n==8 && size==sizeof(void *)); return queue; }
int xQueueSend(void *q,const void *value,unsigned delay) { (void)q;(void)delay; if(count==8)return 0;queue[(head+count++)%8]=*(void *const *)value;return 1; }
int xQueueReceive(void *q,void *value,unsigned delay) { (void)q;(void)delay;if(!count)return 0;*(void **)value=queue[head];head=(head+1)%8;count--;return 1; }
void vQueueDelete(void *q) { (void)q; }
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) { (void)fn;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;tasks++;return 1; }
void vTaskDelay(unsigned delay) { now+=delay*1000; }
int64_t esp_timer_get_time(void) { return now; }
bool external_rgb_ready(void) { return true; }
void external_rgb_demo_mode(void) {}
bool external_rgb_remote_set(int i,int r,int g,int b,int bright) { pixels[i][0]=r*bright/100;pixels[i][1]=g*bright/100;pixels[i][2]=b*bright/100; return true; }
void noise_ctrl_send_command_result(uint64_t gen,const char *id,cJSON *r) {
    assert(gen==7);int n=atoi(id); assert(n>=0&&n<256); assert(!seen[n]++);assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));done++;cJSON_Delete(r);
}
static cJSON *invoke(const char *cmd,const char *json,const char *id) { cJSON *p=json?cJSON_Parse(json):NULL; cJSON *r=remote_led_command(cmd,p,id,7);cJSON_Delete(p); return r; }
static void pattern(int id,int duration,const char *mode) {
    char json[256],request[16];snprintf(json,sizeof(json),"{\"pattern\":\"%s\",\"colors\":[[255,0,0],[0,0,255]],\"duration_ms\":%d}",mode,duration);snprintf(request,sizeof(request),"%d",id);
    cJSON *r=invoke("led.pattern",json,request);assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"_async")));cJSON_Delete(r);
}
int main(void) {
    remote_led_init();assert(tasks==1);
    pattern(0,0,"chase");remote_led_poll();assert(pixels[0][0]==255);
    pattern(1,0,"fade");remote_led_poll();assert(done==1&&seen[0]);
    cJSON *r=invoke("led.clear",NULL,"2");assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);remote_led_poll();assert(done==2&&seen[1]);assert(pixels[0][0]==0&&pixels[1][2]==0);
    pattern(2,100,"alternate");now+=100000;remote_led_poll();remote_led_poll();assert(seen[2]==1&&done==3);
    pattern(3,0,"sparkle");r=invoke("led.set","{\"index\":1,\"r\":20,\"g\":40,\"b\":60,\"brightness\":50}","4");assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);remote_led_poll();assert(pixels[1][0]==10&&pixels[1][2]==30&&seen[3]);
    pattern(4,0,"chase");r=invoke("led.pattern","{\"pattern\":\"bogus\",\"colors\":[[1,2,3]]}","5");assert(cJSON_IsFalse(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);remote_led_poll();assert(!seen[4]);
    r=invoke("led.clear",NULL,"6");cJSON_Delete(r);remote_led_poll();assert(seen[4]);
    for(int i=5;i<200;i++) { pattern(i,1,"fade");now+=2000;remote_led_poll(); }
    assert(tasks==1&&count==0&&done==200);
    remote_led_init(); assert(tasks==1);
    for(int i=200;i<=208;i++) pattern(i,0,"chase");
    r=invoke("led.pattern","{\"pattern\":\"fade\",\"colors\":[[255,0,0]]}","209");
    assert(cJSON_IsFalse(cJSON_GetObjectItem(r,"ok")));cJSON_Delete(r);
    remote_led_poll();assert(done==208&&!seen[208]);
    r=invoke("led.clear",NULL,"210");cJSON_Delete(r);remote_led_poll();assert(done==209&&seen[208]);
    puts("replacement, clear, set, expiry, invalid input and 200 completions passed; one worker task; queue saturation preserved every result");
}
