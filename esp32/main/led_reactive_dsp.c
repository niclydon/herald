// SPDX-License-Identifier: Apache-2.0
#include "led_reactive.h"
#include <math.h>
#include <string.h>
const char *reactive_parse(cJSON *p, reactive_config_t *c) {
    *c=(reactive_config_t){.mode=0,.smoothing_ms=120,.sensitivity=1,.colors={{255,150,0},{100,150,255}}};
    if(p && !cJSON_IsObject(p)) return "Parameters must be an object";
    for(cJSON *v=p?p->child:NULL;v;v=v->next) {
        if(!v->string || (strcmp(v->string,"mode")&&strcmp(v->string,"colors")&&strcmp(v->string,"sensitivity")&&strcmp(v->string,"smoothing_ms"))) return "Unknown reactive parameter";
        for(cJSON *prior=p->child;prior!=v;prior=prior->next) if(!strcmp(prior->string,v->string)) return "Duplicate reactive parameter";
    }
    cJSON *v=cJSON_GetObjectItemCaseSensitive(p,"mode");
    if(v) {
        if(!cJSON_IsString(v)) return "mode must be a string";
        const char *names[]={"energy","spectrum","beat","duet","off"}; c->mode=-1;
        for(int i=0;i<5;i++) if(!strcmp(v->valuestring,names[i])) c->mode=i;
        if(c->mode<0) return "Unknown reactive mode";
    }
    v=cJSON_GetObjectItemCaseSensitive(p,"sensitivity");
    if(v) { if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<.1||v->valuedouble>3) return "sensitivity must be 0.1-3.0"; c->sensitivity=v->valuedouble; }
    v=cJSON_GetObjectItemCaseSensitive(p,"smoothing_ms");
    if(v) { if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>1000||v->valuedouble!=v->valueint) return "smoothing_ms must be an integer 0-1000"; c->smoothing_ms=v->valueint; }
    v=cJSON_GetObjectItemCaseSensitive(p,"colors");
    if(v) {
        int count=cJSON_GetArraySize(v);
        if(!cJSON_IsArray(v)||count<1||count>2) return "colors must contain 1-2 RGB arrays";
        for(int i=0;i<count;i++) {
            cJSON *rgb=cJSON_GetArrayItem(v,i);
            if(!cJSON_IsArray(rgb)||cJSON_GetArraySize(rgb)!=3) return "Each color must be [r,g,b]";
            for(int k=0;k<3;k++) { cJSON *n=cJSON_GetArrayItem(rgb,k); if(!cJSON_IsNumber(n)||!isfinite(n->valuedouble)||n->valuedouble<0||n->valuedouble>255||n->valuedouble!=n->valueint) return "RGB channels must be integers 0-255"; c->colors[i][k]=n->valueint; }
        }
        if(count==1) memcpy(c->colors[1],c->colors[0],sizeof(c->colors[0]));
    }
    return NULL;
}
/* Caller supplies 8192 floats of scratch, keeping FFT storage off the LED stack.
 * All rates 8-48 kHz use their original samples; no aliasing resampler. */
void reactive_analyze(const int16_t *pcm,size_t count,unsigned rate,float *scratch,float *rms,float *bands,float *flux_bins) {
    unsigned n=1; while(n<count) n<<=1;
    float sum=0; memset(scratch,0,2*n*sizeof(float));
    float wr=1,wi=0,cr=cosf(6.2831853f/(count-1)),ci=sinf(6.2831853f/(count-1));
    for(size_t i=0;i<count;i++) {
        float x=pcm[i]/32768.f;sum+=x*x;scratch[2*i]=x*(.5f-.5f*wr);
        float next=wr*cr-wi*ci;wi=wr*ci+wi*cr;wr=next;
    }
    *rms=sqrtf(sum/count);
    for(unsigned i=1,j=0;i<n;i++) { unsigned bit=n>>1; for(;j&bit;bit>>=1) j^=bit; j^=bit; if(i<j) { float t=scratch[2*i];scratch[2*i]=scratch[2*j];scratch[2*j]=t; } }
    for(unsigned len=2;len<=n;len<<=1) {
        float cr=cosf(-6.2831853f/len),ci=sinf(-6.2831853f/len);
        for(unsigned base=0;base<n;base+=len) { float wr=1,wi=0;
            for(unsigned j=0;j<len/2;j++) { unsigned a=2*(base+j),b=2*(base+j+len/2);float r=scratch[b]*wr-scratch[b+1]*wi,im=scratch[b]*wi+scratch[b+1]*wr;
                scratch[b]=scratch[a]-r;scratch[b+1]=scratch[a+1]-im;scratch[a]+=r;scratch[a+1]+=im;float next=wr*cr-wi*ci;wi=wr*ci+wi*cr;wr=next;
            }
        }
    }
    memset(bands,0,4*sizeof(float));memset(flux_bins,0,64*sizeof(float));
    for(unsigned k=1;k<n/2;k++) {
        float hz=(float)k*rate/n, power=(scratch[2*k]*scratch[2*k]+scratch[2*k+1]*scratch[2*k+1])/(n*count*.375f);
        if(hz>=60&&hz<=250) { bands[0]+=power; unsigned bin=(unsigned)((hz-60)/3);if(bin<64) flux_bins[bin]+=sqrtf(power); }
        if(hz>=2000&&hz<=8000) bands[1]+=power;
        if(hz>=100&&hz<=300) bands[2]+=power;
        /* Harmonic weight, not source separation. */
        if(hz>300&&hz<=600) bands[2]+=.25f*power;
        if(hz>=300&&hz<=800) bands[3]+=power;
    }
    for(int i=0;i<4;i++) bands[i]=sqrtf(2*bands[i]);
}
void reactive_levels(reactive_state_t *s,const reactive_config_t *c,float rms,const float *bands,float flux,float dt,int64_t now,float *out) {
    float level[2]={0,0};
    if(c->mode==0) level[0]=level[1]=fminf(1,rms*c->sensitivity*3);
    if(c->mode==1||c->mode==3) for(int i=0;i<2;i++) {
        float input=bands[(c->mode==3?2:0)+i];
        float tau=input>s->agc[i]?.1f:2.f;
        s->agc[i]+=(input-s->agc[i])*(1-expf(-dt/tau));
        level[i]=input<.001f?0:fminf(1,input*c->sensitivity/fmaxf(.02f,s->agc[i]));
    }
    if(c->mode==2) {
        float threshold=fmaxf(.015f,s->flux_mean*1.8f)/c->sensitivity;
        if(flux>threshold && now>s->beat_until+80000) s->beat_until=now+80000;
        s->flux_mean+=(flux-s->flux_mean)*(1-expf(-dt/1.f));
        level[0]=level[1]=s->beat_until && now<=s->beat_until?1:s->beat_until?expf(-(now-s->beat_until)/150000.f):0;
    }
    for(int i=0;i<2;i++) { float a=c->smoothing_ms?1-expf(-dt*1000/c->smoothing_ms):1;
        /* A beat's 80 ms white pulse is deliberately unsmoothed. */
        s->envelope[i]=c->mode==2?level[i]:s->envelope[i]+(level[i]-s->envelope[i])*a;out[i]=s->envelope[i];
    }
}
