#ifndef HUMAN_TRACKING_TARGET_H
#define HUMAN_TRACKING_TARGET_H

#include <stdbool.h>
#include <stdint.h>

#include "human_tracking_overlay.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HT_TARGET_VIEW_IDLE = 0,
    HT_TARGET_VIEW_TARGET_READY,
    HT_TARGET_VIEW_FOLLOWING,
    HT_TARGET_VIEW_FOLLOWING_LOST,
} human_tracking_target_view_t;

typedef struct {
    human_tracking_overlay_status_t overlay;
    bool tracking_active;
    human_tracking_target_view_t effective_view;
} human_tracking_target_snapshot_t;

typedef struct {
    uint32_t frame_id;
    uint16_t timestamp_ms;
    uint32_t screen_w;
    uint32_t screen_h;
    int32_t screen_cx;
    int32_t screen_cy;
    int32_t target_x1;
    int32_t target_y1;
    int32_t target_x2;
    int32_t target_y2;
    int32_t target_cx;
    int32_t target_cy;
    int32_t error_x;
    int32_t error_y;
    int32_t box_w;
    int32_t box_h;
    uint32_t box_area;
    uint8_t track_id;
    uint8_t score_pct;
    uint8_t miss_count;
    bool valid;
    bool tracking_active;
    bool predicted;
    bool lost;
    human_tracking_target_view_t effective_view;
} human_tracking_gimbal_target_t;

typedef struct {
    human_tracking_gimbal_target_t target;
    float norm_error_x;
    float norm_error_y;
    float mapped_error_x;
    float mapped_error_y;
    float yaw_cmd;
    float pitch_cmd;
    uint32_t freeze_age_ms;
    bool yaw_in_deadzone;
    bool pitch_in_deadzone;
    bool frozen;
    bool freeze_timed_out;
} human_tracking_gimbal_control_t;

void human_tracking_target_reset(void);
void human_tracking_target_capture(bool tracking_active, human_tracking_target_snapshot_t *out_snapshot);
bool human_tracking_target_consume_snapshot(const human_tracking_target_snapshot_t *snapshot, uint32_t frame_log_every_n);
const char *human_tracking_target_view_name(human_tracking_target_view_t view);
void human_tracking_target_fill_gimbal_output(const human_tracking_target_snapshot_t *snapshot,
                                              human_tracking_gimbal_target_t *out_target);
void human_tracking_target_fill_gimbal_control(const human_tracking_target_snapshot_t *snapshot,
                                               uint32_t now_ms,
                                               human_tracking_gimbal_control_t *out_control);

#ifdef __cplusplus
}
#endif

#endif