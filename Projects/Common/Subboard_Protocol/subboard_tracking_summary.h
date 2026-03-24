#ifndef SUBBOARD_TRACKING_SUMMARY_H
#define SUBBOARD_TRACKING_SUMMARY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBBOARD_TRACKING_STATE_DISABLED        0x00u
#define SUBBOARD_TRACKING_STATE_IDLE            0x01u
#define SUBBOARD_TRACKING_STATE_FOLLOWING       0x02u
#define SUBBOARD_TRACKING_STATE_FOLLOWING_LOST  0x03u

#define SUBBOARD_TRACKING_FLAG_HAS_TARGET       (1u << 0)
#define SUBBOARD_TRACKING_FLAG_CMD_PENDING      (1u << 1)
#define SUBBOARD_TRACKING_FLAG_PREDICTED        (1u << 2)
#define SUBBOARD_TRACKING_FLAG_RAW_STATE_VALID  (1u << 3)
#define SUBBOARD_TRACKING_FLAG_LOST             (1u << 4)

typedef struct __attribute__((packed)) {
    uint8_t valid;
    uint8_t tracking_state;
    uint8_t tracking_flags;
    int8_t selected_idx;
    uint32_t updated_ms;
    uint32_t frame_id;
    int16_t cx;
    int16_t cy;
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
    int8_t vx;
    int8_t vy;
    uint8_t count;
    uint8_t confidence;
    uint8_t target_id;
    uint8_t miss_count;
    uint8_t tracker_state_raw;
    uint8_t tracker_flags_raw;
    uint8_t reserved[6];
} subboard_tracking_summary_t;

#ifdef __cplusplus
}
#endif

#endif