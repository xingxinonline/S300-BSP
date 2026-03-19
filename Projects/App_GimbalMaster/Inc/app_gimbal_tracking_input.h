#ifndef APP_GIMBAL_MASTER_TRACKING_INPUT_H
#define APP_GIMBAL_MASTER_TRACKING_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "subboard_detection_result.h"
#include "subboard_tracking_summary.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*app_gimbal_tracking_input_millis_fn_t)(void);

typedef struct {
    uint32_t updated_ms;
    uint32_t screen_w;
    uint32_t screen_h;
    int32_t screen_cx;
    int32_t screen_cy;
    int32_t target_cx;
    int32_t target_cy;
    int32_t target_x1;
    int32_t target_y1;
    int32_t target_x2;
    int32_t target_y2;
    int32_t error_x;
    int32_t error_y;
    int32_t box_w;
    int32_t box_h;
    int8_t vx;
    int8_t vy;
    uint8_t confidence;
    uint8_t tracking_state;
    uint8_t miss_count;
    uint8_t tracker_state_raw;
    uint8_t tracker_flags_raw;
    bool valid;
    bool tracking_active;
    bool predicted;
    bool lost;
    bool command_pending;
    bool frozen;
    bool freeze_timed_out;
    float norm_error_x;
    float norm_error_y;
    float yaw_cmd;
    float pitch_cmd;
} app_gimbal_tracking_input_output_t;

int app_gimbal_tracking_input_init(app_gimbal_tracking_input_millis_fn_t millis_fn);
void app_gimbal_tracking_input_reset(void);
void app_gimbal_tracking_input_tick(void);
void app_gimbal_tracking_input_handle_card1_result(const subboard_detection_result_t *result);
void app_gimbal_tracking_input_handle_card1_tracking(const subboard_tracking_summary_t *summary);
void app_gimbal_tracking_input_get_output(app_gimbal_tracking_input_output_t *out_output);

#ifdef __cplusplus
}
#endif

#endif