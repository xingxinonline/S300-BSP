#ifndef APP_GIMBAL_MASTER_MEDIA_CONTROL_H
#define APP_GIMBAL_MASTER_MEDIA_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_MEDIA_CONTROL_NO_CHANGE = 0,
    APP_MEDIA_CONTROL_ACCEPTED,
    APP_MEDIA_CONTROL_FAILED,
} app_media_control_result_t;

void app_media_control_init(void);
app_media_control_result_t app_media_control_trigger_photo(void);
app_media_control_result_t app_media_control_set_recording(bool enabled);
uint32_t app_media_control_get_photo_count(void);

#ifdef __cplusplus
}
#endif

#endif