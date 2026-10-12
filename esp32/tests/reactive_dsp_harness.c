#include "led_reactive.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int16_t pcm[2400];static float scratch[8192];
static void analyze(float hz,unsigned rate,float *bands,float *rms,float *bins) {
    unsigned count=rate/20;for(unsigned i=0;i<count;i++) pcm[i]=(int16_t)(12000*sinf(6.2831853f*hz*i/rate));
    reactive_analyze(pcm,count,rate,scratch,rms,bands,bins);
}
int main(void) {
    reactive_config_t c;assert(!reactive_parse(NULL,&c)&&c.mode==0&&c.smoothing_ms==120);
    const char *bad[]={"[]","{\"mode\":\"x\"}","{\"mode\":4}","{\"sensitivity\":0}","{\"sensitivity\":3.1}","{\"smoothing_ms\":1.5}","{\"colors\":[]}","{\"colors\":[[1,2]]}","{\"colors\":[[1,2,256]]}","{\"foo\":1}","{\"mode\":\"beat\",\"mode\":\"off\"}"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) { cJSON *p=cJSON_Parse(bad[i]);assert(reactive_parse(p,&c));cJSON_Delete(p); }
    const char *modes[]={"energy","spectrum","beat","duet","off"};
    for(int i=0;i<5;i++) { char json[100];snprintf(json,sizeof(json),"{\"mode\":\"%s\",\"colors\":[[10,20,30]]}",modes[i]);cJSON *p=cJSON_Parse(json);assert(!reactive_parse(p,&c)&&c.mode==i&&c.colors[1][2]==30);cJSON_Delete(p); }
    float bands[4],rms,bins[64];
    for(unsigned rate=8000;rate<=48000;rate+=8000) {
        analyze(120,rate,bands,&rms,bins);assert(rms>.24&&rms<.28);assert(bands[0]>bands[1]*10);
        analyze(3000,rate,bands,&rms,bins);assert(bands[1]>bands[0]*10);
        analyze(180,rate,bands,&rms,bins);assert(bands[2]>bands[3]*4);
        analyze(700,rate,bands,&rms,bins);assert(bands[3]>bands[2]*4);
    }
    reactive_parse(NULL,&c);reactive_state_t s={0};float out[2];c.smoothing_ms=0;
    reactive_levels(&s,&c,.1f,bands,0,.05,50000,out);assert(fabsf(out[0]-.3f)<.001);
    reactive_levels(&s,&c,0,bands,0,.05,100000,out);assert(out[0]==0);
    c.smoothing_ms=120;memset(&s,0,sizeof(s));reactive_levels(&s,&c,.1,bands,0,.05,100000,out);assert(out[0]>0&&out[0]<.3);
    for(int i=0;i<30;i++) reactive_levels(&s,&c,0,bands,0,.05,150000+i*50000,out);assert(out[0]<.001);
    c.mode=2;memset(&s,0,sizeof(s));
    /* Synthetic low-frequency click: silence -> short bass burst -> silence. */
    memset(pcm,0,sizeof(pcm));for(int i=0;i<160;i++) pcm[i]=(int16_t)(30000*sinf(6.2831853f*120*i/16000));
    reactive_analyze(pcm,800,16000,scratch,&rms,bands,bins);float flux=0;for(int i=0;i<64;i++) flux+=bins[i];assert(flux>.015);
    reactive_levels(&s,&c,rms,bands,flux,.05,1000000,out);assert(out[0]==1);
    reactive_levels(&s,&c,0,bands,0,.05,1070000,out);assert(out[0]==1);
    reactive_levels(&s,&c,0,bands,0,.05,1200000,out);assert(out[0]>0&&out[0]<1);
    reactive_levels(&s,&c,0,bands,0,.05,3000000,out);assert(out[0]<.001);
    puts("reactive DSP validation, RMS, smoothing, frequency bands and onset passed");
}
