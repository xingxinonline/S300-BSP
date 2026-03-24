#ifndef SUBBOARD_FACE_RECOGNITION_PROTO_H
#define SUBBOARD_FACE_RECOGNITION_PROTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBBOARD_FR_MAX_DETECTION_COUNT          10u
#define SUBBOARD_FR_RESULT_MAGIC                 0x44455446u
#define SUBBOARD_FR_PROTOCOL_VERSION             0x0203u
#define SUBBOARD_FR_FEATURE_DIMENSION            128u
#define SUBBOARD_FR_RESULT_FLAG_HAS_FEATURE      0x00000001u

typedef struct __attribute__((packed)) {
    float    score;
    int32_t  x1;
    int32_t  y1;
    int32_t  x2;
    int32_t  y2;
    float    lm[10];
    uint8_t  type;
    uint8_t  track_id;
    int8_t   vx;
    int8_t   vy;
    uint8_t  speed;
    uint8_t  kf_confidence;
    uint8_t  edge_flags;
    uint8_t  reserved;
} subboard_fr_detection_box_t;

typedef struct __attribute__((packed)) {
    uint32_t                    magic;
    uint32_t                    version;
    uint32_t                    frame_id;
    uint32_t                    timestamp;
    uint32_t                    count;
    int32_t                     selected_idx;
    subboard_fr_detection_box_t boxes[SUBBOARD_FR_MAX_DETECTION_COUNT];
    uint32_t                    feature_flags;
    uint16_t                    feature_dim;
    int8_t                      feature_vector[SUBBOARD_FR_FEATURE_DIMENSION];
} subboard_fr_detection_result_t;

#ifdef __cplusplus
}
#endif

#endif