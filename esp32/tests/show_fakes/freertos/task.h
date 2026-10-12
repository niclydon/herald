#pragma once
#include "FreeRTOS.h"
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,void *);
void vTaskDelay(unsigned);
void vTaskDelete(void *);
