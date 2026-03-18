#ifndef APP_GIMBAL_MASTER_CARD3_RESULT_HANDLER_H
#define APP_GIMBAL_MASTER_CARD3_RESULT_HANDLER_H

#include <stdint.h>

#include "subboard_detection_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*app_card3_result_handler_millis_fn_t)(void);

void app_card3_result_handler_init(app_card3_result_handler_millis_fn_t millis_fn);
void app_card3_result_handler_handle_result(const subboard_detection_result_t *result);
void app_card3_result_handler_notify_offline(void);

#ifdef __cplusplus
}
#endif

#endif