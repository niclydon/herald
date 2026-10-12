// SPDX-License-Identifier: Apache-2.0
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "herald_lan.c"
const unsigned char cert_start[]={0},cert_end[]={0},key_start[]={0},key_end[]={0};
static int64_t now;static int invoked;
int64_t esp_timer_get_time(void){return now;}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void *)1;}
int xSemaphoreTake(SemaphoreHandle_t h,unsigned t){(void)h;(void)t;return 1;}
void xSemaphoreGive(SemaphoreHandle_t h){(void)h;}
void vSemaphoreDelete(SemaphoreHandle_t h){(void)h;}
int httpd_req_get_hdr_value_str(httpd_req_t*r,const char*k,char*b,size_t n){const char*v=!strcmp(k,"Authorization")?r->auth:r->owner;if(!v||strlen(v)>=n)return -1;strcpy(b,v);return 0;}
int httpd_resp_set_status(httpd_req_t*r,const char*s){r->status=s;return 0;}
int httpd_resp_set_type(httpd_req_t*r,const char*s){(void)r;(void)s;return 0;}
int httpd_resp_set_hdr(httpd_req_t*r,const char*k,const char*v){(void)r;(void)k;(void)v;return 0;}
int httpd_resp_send(httpd_req_t*r,const char*b,int n){(void)n;r->response=strdup(b);return 0;}
int httpd_req_recv(httpd_req_t*r,char*b,size_t n){memcpy(b,r->body+r->offset,n);r->offset+=(int)n;return (int)n;}
int httpd_ssl_start(httpd_handle_t*h,httpd_ssl_config_t*c){assert(c->port_secure==8443);*h=(void*)1;return 0;}
int httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t*u){(void)h;(void)u;return 0;}
void noise_ctrl_lan_invoke(const char*json){cJSON*j=cJSON_Parse(json);assert(j);invoked++;cJSON_Delete(j);}
static httpd_req_t req(const char*id,const char*owner,const char*cmd){
 static char body[512];snprintf(body,sizeof body,"{\"id\":\"%s\",\"owner\":\"%s\",\"command\":\"%s\",\"params\":{}}",id,owner,cmd);
 return (httpd_req_t){.uri="/v1/commands",.body=body,.auth="Bearer " CONFIG_HOMEHUB_LAN_TOKEN,.content_len=(int)strlen(body)};
}
static void check(httpd_req_t*r,const char*status){post(r);assert(!strcmp(r->status,status));free(r->response);r->response=NULL;r->offset=0;}
int main(void){
 herald_lan_start();httpd_req_t r=req("a","one","led.clear");r.auth="wrong";check(&r,"401 Unauthorized");assert(invoked==0);
 r=req("a","one","led.clear");r.content_len=BODY_MAX+1;check(&r,"413 Payload Too Large");assert(invoked==0);
 r=req("a","one","device.ota");check(&r,"403 Forbidden");assert(invoked==0);
 r=req("a","one","led.clear");check(&r,"202 Accepted");assert(invoked==1);
 r=req("a","one","led.clear");check(&r,"200 OK");assert(invoked==1);
 r=req("a","one","led.set");check(&r,"409 Conflict");assert(invoked==1);
 r=req("b","two","led.clear");check(&r,"409 Conflict");assert(invoked==1);
 r=req("read","two","device.info");check(&r,"202 Accepted");assert(invoked==2);
 cJSON *result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"ok",true);herald_lan_result("a",result);
 result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"ok",false);herald_lan_result("a",result);
 assert(cJSON_IsTrue(cJSON_GetObjectItem(jobs[0].result,"ok")));
 now=121000000;r=req("b","two","led.clear");check(&r,"202 Accepted");assert(invoked==3);
 for(int i=0;i<JOBS;i++){free(jobs[i].request);cJSON_Delete(jobs[i].result);memset(&jobs[i],0,sizeof(jobs[i]));}
 puts("HTTPS auth, body bounds, admin policy, idempotency, lease isolation and completion ownership passed");
}
