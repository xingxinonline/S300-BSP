#ifndef APP_GIMBAL_MASTER_ACTION_EXECUTOR_H
#define APP_GIMBAL_MASTER_ACTION_EXECUTOR_H

#include <stdbool.h>

#include "app_action_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_ACTION_EXECUTE_IGNORED = 0,
    APP_ACTION_EXECUTE_DONE,
    APP_ACTION_EXECUTE_DEFERRED,
    APP_ACTION_EXECUTE_FAILED,
} app_action_execute_result_t;

app_action_execute_result_t app_action_execute(const app_action_t *action);
const char *app_action_execute_result_name(app_action_execute_result_t result);

#ifdef __cplusplus
}
#endif

#endif