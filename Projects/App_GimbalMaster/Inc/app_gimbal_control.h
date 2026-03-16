#ifndef APP_GIMBAL_MASTER_GIMBAL_CONTROL_H
#define APP_GIMBAL_MASTER_GIMBAL_CONTROL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_GIMBAL_CONTROL_NO_CHANGE = 0,
    APP_GIMBAL_CONTROL_ACCEPTED,
} app_gimbal_control_result_t;

void app_gimbal_control_init(void);
app_gimbal_control_result_t app_gimbal_control_set_tracking(bool enabled);
bool app_gimbal_control_is_tracking(void);

#ifdef __cplusplus
}
#endif

#endif