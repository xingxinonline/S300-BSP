#ifndef APP_GIMBAL_MASTER_RUNTIME_STATE_H
#define APP_GIMBAL_MASTER_RUNTIME_STATE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_RUNTIME_STATE_IDLE = 0,
    APP_RUNTIME_STATE_RECORDING,
    APP_RUNTIME_STATE_TRACKING,
    APP_RUNTIME_STATE_TRACKING_RECORDING,
} app_runtime_state_t;

typedef enum {
    APP_RUNTIME_EVENT_NONE = 0,
    APP_RUNTIME_EVENT_PHOTO,
    APP_RUNTIME_EVENT_RECORD_START,
    APP_RUNTIME_EVENT_RECORD_STOP,
    APP_RUNTIME_EVENT_TRACK_START,
    APP_RUNTIME_EVENT_TRACK_STOP,
    APP_RUNTIME_EVENT_FILL_LIGHT_ON,
    APP_RUNTIME_EVENT_FILL_LIGHT_OFF,
} app_runtime_event_t;

typedef enum {
    APP_RUNTIME_EVENT_NO_CHANGE = 0,
    APP_RUNTIME_EVENT_APPLIED,
} app_runtime_event_result_t;

typedef struct {
    app_runtime_state_t state;
    bool recording;
    bool tracking;
    bool fill_light_enabled;
} app_runtime_snapshot_t;

void app_runtime_state_reset(void);
app_runtime_event_result_t app_runtime_state_preview_event(app_runtime_event_t event,
                                                           app_runtime_snapshot_t *next_snapshot);
app_runtime_event_result_t app_runtime_state_apply_event(app_runtime_event_t event);
app_runtime_state_t app_runtime_state_get(void);
void app_runtime_state_get_snapshot(app_runtime_snapshot_t *snapshot);
bool app_runtime_state_is_recording(void);
bool app_runtime_state_is_tracking(void);
bool app_runtime_state_is_fill_light_enabled(void);
const char *app_runtime_state_name(app_runtime_state_t state);
const char *app_runtime_event_name(app_runtime_event_t event);
const char *app_runtime_event_result_name(app_runtime_event_result_t result);

#ifdef __cplusplus
}
#endif

#endif