// Two resistor-protected common-cathode LEDs, physically verified pin order.
#include "driver/ledc.h"
#include "esp_log.h"
#include <math.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#if CONFIG_HOMEHUB_REMOTE_MANAGEMENT
#include "remote_device.h"
#endif
static bool ready;
static SemaphoreHandle_t hardware_lock;
static atomic_bool remote_owned;
bool external_rgb_ready(void) { return ready; }
static const int pins[6]={17,16,15,9,7,8}; // R,G,B for LEDs one and two
void external_rgb_off(void) {
    if(!ready || atomic_load(&remote_owned)) return;
    if(xSemaphoreTake(hardware_lock,0)!=pdTRUE) return;
    for(int i=0;i<6;i++) { ledc_set_duty(LEDC_LOW_SPEED_MODE,i,0); ledc_update_duty(LEDC_LOW_SPEED_MODE,i); }
    xSemaphoreGive(hardware_lock);
}
void external_rgb_frame(float seconds,float energy,int style) {
    if(!ready || atomic_load(&remote_owned)) return;
    if(xSemaphoreTake(hardware_lock,0)!=pdTRUE) return;
    if(atomic_load(&remote_owned)) { xSemaphoreGive(hardware_lock); return; }
    for(int led=0;led<2;led++) {
        float hue=seconds*.18f+led*.5f;
        float brightness=fminf(.65f,.12f+energy*1.7f);
#if CONFIG_HOMEHUB_REMOTE_MANAGEMENT
        brightness*=remote_default_brightness()/100.f;
#endif
        for(int color=0;color<3;color++) {
            float value=(1.f+cosf(6.2831853f*(hue-color/3.f)))*.5f;
            if(style==0) value=color==((int)(seconds*.8f)%3)?1.f:0.f;
            if(style==2) value=(1.f+sinf(seconds*5.f+led*3.14159265f))*.5f;
            int duty=(int)(255.f*brightness*value);
            ledc_set_duty(LEDC_LOW_SPEED_MODE,led*3+color,duty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE,led*3+color);
        }
    }
    xSemaphoreGive(hardware_lock);
}
void external_rgb_demo_mode(void) { atomic_store(&remote_owned,false); }
bool external_rgb_remote_set(int index,int r,int g,int b,int brightness) {
    if(!ready || index<0 || index>1 || xSemaphoreTake(hardware_lock,0)!=pdTRUE) return false;
    atomic_store(&remote_owned,true);
    int rgb[3]={r,g,b};
#if CONFIG_HOMEHUB_REMOTE_MANAGEMENT
    brightness=brightness*remote_default_brightness()/100;
#endif
    for(int k=0;k<3;k++) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE,index*3+k,rgb[k]*brightness/100);
        ledc_update_duty(LEDC_LOW_SPEED_MODE,index*3+k);
    }
    xSemaphoreGive(hardware_lock); return true;
}
void external_rgb_test_start(void) {
    hardware_lock=xSemaphoreCreateMutex();
    if(!hardware_lock) return;
    ledc_timer_config_t timer={.speed_mode=LEDC_LOW_SPEED_MODE,.timer_num=LEDC_TIMER_1,
        .duty_resolution=LEDC_TIMER_8_BIT,.freq_hz=5000,.clk_cfg=LEDC_AUTO_CLK};
    if(ledc_timer_config(&timer)!=ESP_OK) return;
    int count=0;
    for(int i=0;i<6;i++) {
        ledc_channel_config_t ch={.gpio_num=pins[i],.speed_mode=LEDC_LOW_SPEED_MODE,
            .channel=i,.timer_sel=LEDC_TIMER_1,.duty=0};
        if(ledc_channel_config(&ch)!=ESP_OK) {
            for(int j=0;j<count;j++) ledc_stop(LEDC_LOW_SPEED_MODE,j,0);
            ESP_LOGE("link.rgb","LED initialization failed"); return;
        }
        count++;
    }
    ready=true;
    ESP_LOGI("link.rgb","two RGB LEDs ready: RGB=17/16/15 and 9/7/8");
}
