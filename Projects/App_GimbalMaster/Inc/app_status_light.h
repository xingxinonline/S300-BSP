#ifndef APP_STATUS_LIGHT_H
#define APP_STATUS_LIGHT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_STATUS_LIGHT_MODE_DISABLED = 0,
    APP_STATUS_LIGHT_MODE_BOOT,
    APP_STATUS_LIGHT_MODE_WAIT_SUBBOARD,
    APP_STATUS_LIGHT_MODE_SUBBOARD_READY,
    APP_STATUS_LIGHT_MODE_KWS_RUNNING,
    APP_STATUS_LIGHT_MODE_ERROR,
} app_status_light_mode_t;

int app_status_light_init(uint32_t (*get_millis_fn)(void));
void app_status_light_set_mode(app_status_light_mode_t mode);
void app_status_light_notify_kws_hit(uint8_t keyword_idx);
void app_status_light_tick(void);

#ifdef __cplusplus
}
#endif

#endif