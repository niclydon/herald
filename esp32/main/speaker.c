// SPDX-License-Identifier: Apache-2.0
// Bare N16R8 board and NS4168: wiring confirmed by the user's labeled photos.
// GPIO4=BCLK, GPIO5=LRCLK, GPIO6=DIN, amplifier V=3V3, G=GND.
#include "sdkconfig.h"
#include "speaker.h"
#include "esp_timer.h"
#if CONFIG_HOMEHUB_SHOW
#include "show.h"
#endif
#include "speaker_wav.h"
#include "cJSON.h"
#include "driver/i2s_std.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#if CONFIG_HOMEHUB_REMOTE_MANAGEMENT
#include "remote_device.h"
#endif
#if CONFIG_HOMEHUB_REMOTE_LEDS
#include "remote_led.h"
#endif
#define AUDIO_MAX (2*1024*1024)
#if CONFIG_HOMEHUB_EXTERNAL_RGB_TEST
void external_rgb_frame(float seconds,float energy,int style);
void external_rgb_off(void);
static bool demo_active;
static int demo_style=1;
static float demo_seconds;
#endif
#if CONFIG_HOMEHUB_LED_REACTIVE
#include "led_reactive.h"
static unsigned output_rate=16000;
#endif
static i2s_chan_handle_t tx;
static SemaphoreHandle_t lock;
static atomic_bool stop_requested;
static const char *TAG="link.speaker";
typedef struct { char *url, *text, *id; bool demo; int volume; noise_ctrl_session_generation_t generation; } job_t;
static cJSON *result(bool ok, const char *message) {
    cJSON *r=cJSON_CreateObject();
    if (!r) return NULL;
    cJSON_AddBoolToObject(r,"ok",ok);
    if (ok) { cJSON *p=cJSON_AddObjectToObject(r,"payload"); cJSON_AddStringToObject(p,"status",message); }
    else { cJSON *e=cJSON_AddObjectToObject(r,"error"); cJSON_AddStringToObject(e,"message",message); }
    return r;
}
static esp_err_t rate(uint32_t hz) {
#if CONFIG_HOMEHUB_LED_REACTIVE
    output_rate=hz;
#endif
    i2s_std_clk_config_t clk=I2S_STD_CLK_DEFAULT_CONFIG(hz);
    esp_err_t e=i2s_channel_reconfig_std_clock(tx,&clk);
    return e==ESP_OK ? i2s_channel_enable(tx) : e;
}
static bool write_samples(const int16_t *pcm, size_t bytes) {
    if(atomic_load(&stop_requested)) return false;
#if CONFIG_HOMEHUB_EXTERNAL_RGB_TEST
    if(demo_active && bytes) {
        float energy=0;
        for(size_t i=0;i<bytes/2;i++) energy+=fabsf(pcm[i]/32768.f);
        external_rgb_frame(demo_seconds,energy/(bytes/2),demo_style);
    }
#endif
    size_t done=0;
    esp_err_t e=i2s_channel_write(tx,pcm,bytes,&done,1000);
#if CONFIG_HOMEHUB_LED_REACTIVE
    if(done) led_reactive_tap(pcm,done/4,output_rate);
#endif
    return e==ESP_OK && done==bytes;
}
static void finish_audio(void) {
    int16_t zeros[512]={0};
    for(int i=0;i<4;i++) write_samples(zeros,sizeof(zeros));
    vTaskDelay(pdMS_TO_TICKS(50));
    i2s_channel_disable(tx);
}
static bool tone(int volume) {
    if (rate(16000)!=ESP_OK) return false;
    int16_t pcm[512]; bool ok=true;
    // 300 ms with a short fade, duplicated in both channels for the NS4168.
    for(int base=0;base<4800 && ok;base+=256) {
        int frames=4800-base<256?4800-base:256;
        for(int i=0;i<frames;i++) {
            int t=base+i; float fade=fminf(1.f,fminf(t/160.f,(4800-t)/160.f));
            int16_t s=(int16_t)(32767.f*volume/100.f*fade*sinf(2.f*3.14159265f*660.f*t/16000.f));
            pcm[i*2]=pcm[i*2+1]=s;
        }
        ok=write_samples(pcm,frames*4);
    }
    finish_audio(); return ok;
}
static uint8_t *download(const char *url,const char *text,size_t *n) {
    esp_http_client_config_t cfg={.url=url,.timeout_ms=text?240000:10000,.crt_bundle_attach=esp_crt_bundle_attach,
        .disable_auto_redirect=true,.buffer_size=2048};
    esp_http_client_handle_t h=esp_http_client_init(&cfg);
    if (!h) return NULL;
    uint8_t *buf=NULL; char *body=NULL; *n=0;
    if(text) {
        cJSON *json=cJSON_CreateObject();
        if(!json) goto out;
        cJSON_AddStringToObject(json,"text",text); body=cJSON_PrintUnformatted(json); cJSON_Delete(json);
        if(!body) goto out;
        char bearer[192];
        int len=snprintf(bearer,sizeof(bearer),"Bearer %s",CONFIG_HOMEHUB_SPEAKER_TTS_TOKEN);
        if(len<0 || len>=(int)sizeof(bearer)) goto out;
        esp_http_client_set_method(h,HTTP_METHOD_POST);
        esp_http_client_set_header(h,"Content-Type","application/json");
        esp_http_client_set_header(h,"Authorization",bearer);
    }
    if (esp_http_client_open(h,body?strlen(body):0)!=ESP_OK) goto out;
    if(body) {
        size_t pos=0, len=strlen(body);
        while(pos<len) { int sent=esp_http_client_write(h,body+pos,len-pos); if(sent<=0) goto out; pos+=sent; }
    }
    int64_t length=esp_http_client_fetch_headers(h);
    if (length<0 || length>AUDIO_MAX || esp_http_client_get_status_code(h)!=200) goto out;
    buf=heap_caps_malloc(AUDIO_MAX+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!buf) goto out;
    while(*n<=AUDIO_MAX) {
        if(atomic_load(&stop_requested)) { free(buf); buf=NULL; break; }
        int got=esp_http_client_read(h,(char*)buf+*n,AUDIO_MAX+1-*n);
        if(got<0) { free(buf); buf=NULL; break; }
        if(!got) break;
        *n+=got;
    }
    if(buf && (*n>AUDIO_MAX || !esp_http_client_is_complete_data_received(h))) { free(buf); buf=NULL; }
out:
    if(buf) {
        uint8_t *small=heap_caps_realloc(buf,*n?*n:1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!small) { free(buf); buf=NULL; } else buf=small;
    }
    free(body); esp_http_client_close(h); esp_http_client_cleanup(h); return buf;
}
static bool play_timed(const uint8_t *data,size_t n,int volume,void (*started)(int64_t,void *),void *arg) {
    speaker_wav_t w;
    if(!speaker_parse_wav(data,n,&w) || rate(w.rate)!=ESP_OK) return false;
    if(started) started(esp_timer_get_time(),arg);
    int16_t pcm[512]; bool ok=true;
    for(size_t pos=0;pos<w.bytes && ok;) {
        size_t frames=(w.bytes-pos)/(w.channels*2); if(frames>256) frames=256;
        for(size_t i=0;i<frames;i++) {
            const uint8_t *p=data+w.offset+pos+i*w.channels*2;
            int a=(int16_t)speaker_le16(p), b=w.channels==2?(int16_t)speaker_le16(p+2):a;
            int16_t s=(int16_t)(((a+b)/2)*volume/100);
            pcm[i*2]=pcm[i*2+1]=s;
        }
        ok=write_samples(pcm,frames*4); pos+=frames*w.channels*2;
#if CONFIG_HOMEHUB_EXTERNAL_RGB_TEST
        if(demo_active) demo_seconds+=(float)frames/w.rate;
#endif
    }
    finish_audio(); return ok;
}
static bool play(const uint8_t *data,size_t n,int volume) {
    return play_timed(data,n,volume,NULL,NULL);
}
#if CONFIG_HOMEHUB_SHOW
bool speaker_show_claim(void) {
    if(!tx||!lock||xSemaphoreTake(lock,0)!=pdTRUE) return false;
    atomic_store(&stop_requested,false); return true;
}
void speaker_show_release(void) { xSemaphoreGive(lock); }
void speaker_show_cancel(void) { atomic_store(&stop_requested,true); }
bool speaker_show_cancelled(void) { return atomic_load(&stop_requested); }
uint8_t *speaker_show_prepare(const char *url,size_t *n) {
    uint8_t *data=download(url,NULL,n); speaker_wav_t wav;
    if(data&&!speaker_parse_wav(data,*n,&wav)) { free(data);return NULL; }
    return data;
}
bool speaker_show_play(const uint8_t *data,size_t n,int volume,void (*started)(int64_t,void *),void *arg) {
    if(atomic_load(&stop_requested)) return false;
    return play_timed(data,n,volume,started,arg);
}
#endif
#if CONFIG_HOMEHUB_EXTERNAL_RGB_TEST
static bool demo_music(int volume) {
    const float notes[]={261.63f,329.63f,392.f,523.25f,392.f,329.63f,440.f,523.25f};
    if(rate(16000)!=ESP_OK) return false;
    int16_t pcm[512]; bool ok=true;
    for(int beat=0;beat<16 && ok;beat++) {
        for(int base=0;base<4000 && ok;base+=256) {
            int frames=4000-base<256?4000-base:256;
            for(int i=0;i<frames;i++) {
                float t=(base+i)/16000.f;
                float env=fminf(1.f,t/.01f)*expf(-t*10.f);
                float note=notes[beat%8];
                float sound=.24f*env*(sinf(6.2831853f*note*t)+.3f*sinf(6.2831853f*note*2*t));
                int16_t value=(int16_t)(32767.f*sound*volume/100.f);
                pcm[2*i]=pcm[2*i+1]=value;
            }
            ok=write_samples(pcm,frames*4); demo_seconds+=(float)frames/16000;
        }
    }
    finish_audio(); return ok;
}
static bool demo_show(int volume) {
#if CONFIG_HOMEHUB_REMOTE_LEDS
    remote_led_demo_claim();
#endif
    static const char *lines[]={
#if CONFIG_HOMEHUB_REMOTE_MANAGEMENT
        "Welcome to the workbench. I'm " CONFIG_HOMEHUB_FRIENDLY_NAME ", " CONFIG_HOMEHUB_SHORT_NAME " for short, and this little board just got a personality. Two lights, one speaker, and a surprising amount of attitude.",
#else
        "Welcome to the workbench. I'm Muse, and this little board just got a personality. Two lights, one speaker, and a surprising amount of attitude.",
#endif
        "Each light contains red, green, and blue elements. Six pins control them independently. Resistors limit the current, and rapid pulses control brightness. Watch the colors blend and trade places.",
        "My voice is generated by a speech service on the network. Audio travels over Wi Fi to this ESP thirty two board. An I squared S amplifier turns digital samples into sound from the speaker.",
        "Now the lights are following the strength of the audio actually playing. This is software choreography, not a microphone listening to the room. Let's give those wires a little rhythm.",
        "This started with a bare board, jumper wires, and the open source Muse gadget SDK. We added two lights, a speaker, and remote commands, then tested every color and connected spoken replies. Muse can now trigger this whole show with one command.",
        "From a bare board to a tiny talking light show. Built at home, powered by curiosity, and held together by an entirely reasonable number of jumper wires. Take a bow, little gadget."
    };
    uint8_t *clips[6]={0}; size_t sizes[6]={0}; bool ok=false;
    ESP_LOGI(TAG,"demo: preparing six narration clips");
    for(int i=0;i<6;i++) {
        if(atomic_load(&stop_requested)) goto cleanup;
        clips[i]=download(CONFIG_HOMEHUB_SPEAKER_TTS_URL,lines[i],&sizes[i]);
        speaker_wav_t parsed;
        if(!clips[i] || !speaker_parse_wav(clips[i],sizes[i],&parsed)) goto cleanup;
    }
    ESP_LOGI(TAG,"demo: ready; recording countdown 20 seconds");
    for(int i=20;i>0;i--) {
        if(atomic_load(&stop_requested)) goto cleanup;
        ESP_LOGI(TAG,"demo: starts in %d",i);
        external_rgb_frame(20-i,.03f,2); vTaskDelay(pdMS_TO_TICKS(1000));
    }
    demo_active=true; demo_seconds=0; demo_style=1;
    ok=demo_music(volume);
    for(int i=0;i<6 && ok;i++) {
        demo_style=i==0?0:i==4?2:1;
        ok=play(clips[i],sizes[i],volume);
        if(i==3 && ok) { demo_style=1; ok=demo_music(volume); }
    }
    if(ok) { demo_style=1; ok=demo_music(volume); }
cleanup:
    demo_active=false; external_rgb_off();
    for(int i=0;i<6;i++) free(clips[i]);
    return ok;
}
#endif
static void worker(void *arg) {
    job_t *j=arg; bool ok; size_t n=0;
#if CONFIG_HOMEHUB_EXTERNAL_RGB_TEST
    if(j->demo) ok=demo_show(j->volume);
    else
#endif
    if(j->url) { uint8_t *data=download(j->url,j->text,&n); ok=data && play(data,n,j->volume); free(data); }
    else ok=tone(j->volume);
    ESP_LOGI(TAG,"%s: %s",j->demo?"demo":j->text?"speech":j->url?"WAV playback":"test tone",ok?"completed":"failed");
    if(j->id) noise_ctrl_send_command_result(j->generation,j->id,
        result(ok,ok?"playback completed":j->demo?"Show failed: check the TTS service and available memory":j->text?"Speech failed: check the configured the TTS service endpoint":"Audio failed: use a reachable URL with PCM16 WAV, 8-48 kHz, mono/stereo, at most 2 MB"));
    free(j->url); free(j->text); free(j->id); free(j); xSemaphoreGive(lock); vTaskDelete(NULL);
}
cJSON *speaker_stop_command(cJSON *params) {
    if(params && (!cJSON_IsObject(params)||cJSON_GetArraySize(params))) {
        cJSON *r=result(false,"audio.stop takes no parameters");
        cJSON_AddStringToObject(cJSON_GetObjectItem(r,"error"),"code","invalid_params"); return r;
    }
#if CONFIG_HOMEHUB_SHOW
    show_request_stop();
#endif
    atomic_store(&stop_requested,true);
    return result(true,"stop requested");
}
cJSON *speaker_command(const char *command,cJSON *params,const char *id,noise_ctrl_session_generation_t gen) {
    if(!tx || !lock) return result(false,"speaker is unavailable");
    cJSON *v=cJSON_GetObjectItemCaseSensitive(params,"volume"); int volume=!strcmp(command,"demo.show")?100:75;
#if CONFIG_HOMEHUB_REMOTE_MANAGEMENT
    if(strcmp(command,"demo.show")) volume=remote_default_volume();
#endif
    if(v) {
        if(!cJSON_IsNumber(v) || v->valuedouble<0 || v->valuedouble>100 || v->valuedouble!=v->valueint) {
            return result(false,"volume must be an integer from 0 to 100");
        }
        volume=v->valueint;
    }
    const char *url=NULL, *text=NULL;
    if(!strcmp(command,"audio.speak")) {
        cJSON *t=cJSON_GetObjectItemCaseSensitive(params,"text");
        if(!cJSON_IsString(t) || !t->valuestring || !strlen(t->valuestring) || strlen(t->valuestring)>1000)
            return result(false,"text must contain 1-1000 UTF-8 bytes");
        if(!strlen(CONFIG_HOMEHUB_SPEAKER_TTS_URL)) return result(false,"speech endpoint is not configured");
        text=t->valuestring; url=CONFIG_HOMEHUB_SPEAKER_TTS_URL;
    }
    if(!strcmp(command,"audio.play")) {
        cJSON *u=cJSON_GetObjectItemCaseSensitive(params,"url");
        if(!cJSON_IsString(u) || !u->valuestring || strlen(u->valuestring)>2048 ||
            (strncmp(u->valuestring,"http://",7) && strncmp(u->valuestring,"https://",8)))
            return result(false,"url must be an HTTP(S) WAV URL, at most 2048 characters");
        url=u->valuestring;
    }
    if(!id || !gen) return result(false,"command session is unavailable");
    if(xSemaphoreTake(lock,0)!=pdTRUE) return result(false,"speaker is busy");
    atomic_store(&stop_requested,false);
    job_t *j=calloc(1,sizeof(*j)); cJSON *async=cJSON_CreateObject();
    if(j) { j->demo=!strcmp(command,"demo.show"); j->id=id?strdup(id):NULL; j->url=url?strdup(url):NULL; j->text=text?strdup(text):NULL; j->volume=volume; j->generation=gen; }
    if(!j || (id && !j->id) || (url && !j->url) || (text && !j->text) || !async || !cJSON_AddBoolToObject(async,"_async",true) ||
        xTaskCreate(worker,"speaker",j && j->demo?8192:6144,j,3,NULL)!=pdPASS) {
        if(j) { free(j->id); free(j->url); free(j->text); free(j); } cJSON_Delete(async); xSemaphoreGive(lock);
        return result(false,"cannot start audio worker");
    }
    return async;
}
static void speech_console(void *unused) {
    (void)unused;
    char line[1010]; size_t used=0; bool overflow=false;
    while(true) {
        int c=getchar();
        if(c==EOF) { clearerr(stdin); vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if(c=='\r') continue;
        if(c!='\n') {
            if(used<sizeof(line)-1) line[used++]=(char)c;
            else overflow=true;
            continue;
        }
        line[used]=0;
#if CONFIG_HOMEHUB_EXTERNAL_RGB_TEST
        if(!overflow && !strcmp(line,">demo") && xSemaphoreTake(lock,0)==pdTRUE) {
            atomic_store(&stop_requested,false);
            job_t *j=calloc(1,sizeof(*j));
            if(j) { j->demo=true; j->volume=100; }
            if(!j || xTaskCreate(worker,"speaker",8192,j,3,NULL)!=pdPASS) {
                free(j); xSemaphoreGive(lock); ESP_LOGE(TAG,"cannot start demo");
            }
        }
#endif
        if(!overflow && !strncmp(line,">speak=",7) && used>7 && used-7<=1000 &&
            strlen(CONFIG_HOMEHUB_SPEAKER_TTS_URL) && xSemaphoreTake(lock,0)==pdTRUE) {
            atomic_store(&stop_requested,false);
            job_t *j=calloc(1,sizeof(*j));
            if(j) { j->url=strdup(CONFIG_HOMEHUB_SPEAKER_TTS_URL); j->text=strdup(line+7); j->volume=75; }
#if CONFIG_HOMEHUB_REMOTE_MANAGEMENT
            if(j) j->volume=remote_default_volume();
#endif
            if(!j || !j->url || !j->text || xTaskCreate(worker,"speaker",6144,j,3,NULL)!=pdPASS) {
                if(j) { free(j->url); free(j->text); free(j); } xSemaphoreGive(lock);
                ESP_LOGE(TAG,"cannot start local speech test");
            }
        }
        used=0; overflow=false;
    }
}
void speaker_start(void) {
    lock=xSemaphoreCreateBinary(); if(!lock) return;
    xSemaphoreGive(lock);
    i2s_chan_config_t ch=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    ch.auto_clear=true;
    esp_err_t e=i2s_new_channel(&ch,&tx,NULL);
    i2s_std_config_t cfg={.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=GPIO_NUM_4,.ws=GPIO_NUM_5,.dout=GPIO_NUM_6,.din=I2S_GPIO_UNUSED}};
    if(e==ESP_OK) e=i2s_channel_init_std_mode(tx,&cfg);
    if(e!=ESP_OK) { if(tx) i2s_del_channel(tx); tx=NULL; ESP_LOGE(TAG,"audio initialization failed: %s",esp_err_to_name(e)); return; }
    ESP_LOGI(TAG,"NS4168 ready: BCLK=4 LRCLK=5 DATA=6; default volume=75%%");
    if(strlen(CONFIG_HOMEHUB_SPEAKER_TTS_URL)) {
        xTaskCreate(speech_console,"speech_console",4096,NULL,2,NULL);
        ESP_LOGI(TAG,"text-to-speech configured; USB console accepts >speak=TEXT");
    }
    // Quiet startup tone provides the first physical wiring check.
    job_t *j=calloc(1,sizeof(*j));
    if(j && xSemaphoreTake(lock,0)==pdTRUE) { j->volume=5;
        atomic_store(&stop_requested,false);
        if(xTaskCreate(worker,"speaker",6144,j,3,NULL)!=pdPASS) { free(j); xSemaphoreGive(lock); }
    } else free(j);
}
