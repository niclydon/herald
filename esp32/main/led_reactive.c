// SPDX-License-Identifier: Apache-2.0
#include "led_reactive.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define WINDOW_MAX 2400
/* Single speaker producer; consumer and config live exclusively under LED mutex. */
typedef struct { unsigned epoch,rate,count; int64_t at; int16_t pcm[WINDOW_MAX]; } window_t;
static QueueHandle_t windows;
static window_t *producer,*consumer;
static float *scratch;
static atomic_bool enabled;
static atomic_uint epoch;
static reactive_config_t config;
static reactive_state_t state;
static float rms,bands[4];
static int64_t last_audio,last_render;
static float pending_flux;
bool external_rgb_remote_set(int,int,int,int,int);
bool led_reactive_init(void) {
    if(windows) return true;
    producer=heap_caps_calloc(1,sizeof(*producer),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    consumer=heap_caps_calloc(1,sizeof(*consumer),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    scratch=heap_caps_calloc(8192,sizeof(float),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(producer&&consumer&&scratch) windows=xQueueCreate(2,sizeof(window_t));
    if(windows) return true;
    free(producer);free(consumer);free(scratch);producer=consumer=NULL;scratch=NULL;return false;
}
void led_reactive_begin(const reactive_config_t *c) {
    atomic_store(&enabled,false);config=*c;memset(&state,0,sizeof(state));memset(bands,0,sizeof(bands));
    rms=pending_flux=0;last_audio=0;last_render=esp_timer_get_time();
    atomic_fetch_add(&epoch,1);atomic_store(&enabled,true);
}
void led_reactive_end(void) { atomic_store(&enabled,false);atomic_fetch_add(&epoch,1); }
void led_reactive_tap(const int16_t *pcm,size_t frames,unsigned rate) {
    if(!windows||!atomic_load(&enabled)||rate<8000||rate>48000) return;
    unsigned serial=atomic_load(&epoch);
    if(producer->epoch!=serial||producer->rate!=rate) { producer->count=0;producer->epoch=serial;producer->rate=rate; }
    unsigned target=rate/20;
    for(size_t i=0;i<frames;i++) {
        producer->pcm[producer->count++]=(int16_t)(((int32_t)pcm[2*i]+pcm[2*i+1])/2);
        if(producer->count==target) {
            producer->at=esp_timer_get_time();
            /* Drop new windows under load; never delay the I2S writer. */
            xQueueSend(windows,producer,0);producer->count=0;
        }
    }
}
void led_reactive_render(int64_t now) {
    if(!atomic_load(&enabled)) return;
    unsigned serial=atomic_load(&epoch);
    while(xQueueReceive(windows,consumer,0)==pdTRUE) {
        if(consumer->epoch!=serial||now-consumer->at>150000) continue;
        if(config.mode==0) {
            float sum=0;for(unsigned i=0;i<consumer->count;i++) { float x=consumer->pcm[i]/32768.f;sum+=x*x; }
            rms=sqrtf(sum/consumer->count);last_audio=consumer->at;continue;
        }
        float bins[64];
        reactive_analyze(consumer->pcm,consumer->count,consumer->rate,scratch,&rms,bands,bins);
        float flux=0;for(int i=0;i<64;i++) { flux+=fmaxf(0,bins[i]-state.previous[i]);state.previous[i]=bins[i]; }
        pending_flux=fmaxf(pending_flux,flux);last_audio=consumer->at;
    }
    float dt=fminf(.1f,fmaxf(.001f,(now-last_render)/1000000.f));last_render=now;
    if(!last_audio||now-last_audio>100000) { rms=0;memset(bands,0,sizeof(bands));memset(state.previous,0,sizeof(state.previous)); }
    float out[2];reactive_levels(&state,&config,rms,bands,pending_flux,dt,now,out);pending_flux=0;
    for(int i=0;i<2;i++) {
        bool white=config.mode==2;
        external_rgb_remote_set(i,white?255:config.colors[i][0],white?255:config.colors[i][1],white?255:config.colors[i][2],(int)lroundf(out[i]*100));
    }
}
