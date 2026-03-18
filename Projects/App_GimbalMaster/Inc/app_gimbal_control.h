#ifndef APP_GIMBAL_MASTER_GIMBAL_CONTROL_H
#define APP_GIMBAL_MASTER_GIMBAL_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_GIMBAL_CONTROL_NO_CHANGE = 0,
    APP_GIMBAL_CONTROL_ACCEPTED,
} app_gimbal_control_result_t;

typedef enum {
    APP_GIMBAL_PRESET_NONE = 0,
    APP_GIMBAL_PRESET_PARKING,
    APP_GIMBAL_PRESET_TRACKING,
    APP_GIMBAL_PRESET_CENTER_PREVIEW,
    APP_GIMBAL_PRESET_LEFT_PREVIEW,
    APP_GIMBAL_PRESET_RIGHT_PREVIEW,
    APP_GIMBAL_PRESET_UP_PREVIEW,
} app_gimbal_preset_t;

typedef struct {
    bool ready;
    bool tracking;
    bool yaw_position_valid;
    bool pitch_position_valid;
    int16_t yaw_position;
    int16_t pitch_position;
    float yaw_angle_deg;
    float pitch_angle_deg;
} app_gimbal_control_status_t;

void app_gimbal_control_init(void);
app_gimbal_control_result_t app_gimbal_control_set_tracking(bool enabled);
app_gimbal_control_result_t app_gimbal_control_apply_preset(app_gimbal_preset_t preset);
app_gimbal_control_result_t app_gimbal_control_move_to_angles(float yaw_angle_deg,
                                                              float pitch_angle_deg,
                                                              uint16_t time_ms);
bool app_gimbal_control_read_status(app_gimbal_control_status_t *status);
bool app_gimbal_control_is_tracking(void);
const char *app_gimbal_control_preset_name(app_gimbal_preset_t preset);

#ifdef __cplusplus
}
#endif

#endif