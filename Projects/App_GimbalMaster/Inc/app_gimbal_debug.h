#ifndef APP_GIMBAL_MASTER_GIMBAL_DEBUG_H
#define APP_GIMBAL_MASTER_GIMBAL_DEBUG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*app_gimbal_debug_millis_fn_t)(void);

void app_gimbal_debug_init(app_gimbal_debug_millis_fn_t millis_fn);
void app_gimbal_debug_tick(void);

#ifdef __cplusplus
}
#endif

#endif