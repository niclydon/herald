#pragma once
#include <pthread.h>
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define taskENTER_CRITICAL(m) pthread_mutex_lock(m)
#define taskEXIT_CRITICAL(m) pthread_mutex_unlock(m)
