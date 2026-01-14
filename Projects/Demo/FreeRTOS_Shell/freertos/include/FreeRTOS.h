/* Minimal subset of FreeRTOS header to compile the kernel. */
#ifndef FREERTOS_H
#define FREERTOS_H

#include <stdint.h>
#include <stddef.h>

/* Include the application configuration file. */
#include "FreeRTOSConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Basic types */
typedef void * QueueHandle_t;
typedef void * SemaphoreHandle_t;
typedef uint32_t TickType_t;
typedef int32_t  BaseType_t;
typedef uint32_t UBaseType_t;

/* Compile-time checks */
#ifndef configTICK_RATE_HZ
 #error configTICK_RATE_HZ must be defined in FreeRTOSConfig.h
#endif

#define pdFALSE         ( ( BaseType_t ) 0 )
#define pdTRUE          ( ( BaseType_t ) 1 )
#define pdPASS          ( pdTRUE )
#define pdFAIL          ( pdFALSE )
#define pdMS_TO_TICKS(x) ((TickType_t)((x) * (configTICK_RATE_HZ/1000)))

/* Critical section */
void vPortEnterCritical( void );
void vPortExitCritical( void );
#define taskENTER_CRITICAL() vPortEnterCritical()
#define taskEXIT_CRITICAL()  vPortExitCritical()

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_H */
