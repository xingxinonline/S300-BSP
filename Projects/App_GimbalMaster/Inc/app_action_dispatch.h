#ifndef APP_GIMBAL_MASTER_ACTION_DISPATCH_H
#define APP_GIMBAL_MASTER_ACTION_DISPATCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_ACTION_NONE = 0,
    APP_ACTION_KWS_HIT,
    APP_ACTION_PHOTO,
    APP_ACTION_RECORD_START,
    APP_ACTION_RECORD_STOP,
    APP_ACTION_TRACK_START,
    APP_ACTION_TRACK_STOP,
    APP_ACTION_FILL_LIGHT_ON,
    APP_ACTION_FILL_LIGHT_OFF,
} app_action_type_t;

typedef struct {
    app_action_type_t type;
    uint8_t keyword_idx;
    uint8_t confidence;
    uint32_t chunk_idx;
} app_action_t;

void app_action_dispatch(const app_action_t *action);
void app_action_dispatch_kws_keyword(uint8_t keyword_idx, uint8_t confidence, uint32_t chunk_idx);

#ifdef __cplusplus
}
#endif

#endif