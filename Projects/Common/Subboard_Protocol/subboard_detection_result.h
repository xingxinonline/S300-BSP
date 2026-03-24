#ifndef SUBBOARD_DETECTION_RESULT_H
#define SUBBOARD_DETECTION_RESULT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBBOARD_FACE_VERIFY_STATE_NONE       0u
#define SUBBOARD_FACE_VERIFY_STATE_WAIT_ANCHOR 1u
#define SUBBOARD_FACE_VERIFY_STATE_UNCERTAIN  2u
#define SUBBOARD_FACE_VERIFY_STATE_MATCH      3u
#define SUBBOARD_FACE_VERIFY_STATE_NO_MATCH   4u

#define SUBBOARD_DETECTION_RESERVED_VERIFY_STATE_IDX 0u
#define SUBBOARD_DETECTION_RESERVED_VERIFY_SCORE_IDX 1u
#define SUBBOARD_DETECTION_RESERVED_VERIFY_FLAGS_IDX 2u

#define SUBBOARD_DETECTION_VERIFY_FLAG_ANCHOR_VALID  (1u << 0)
#define SUBBOARD_DETECTION_VERIFY_FLAG_FEATURE_VALID (1u << 1)

typedef struct __attribute__((packed)) {
    uint8_t  valid;
    uint8_t  count;
    uint8_t  type;
    uint8_t  selected_idx;
    int16_t  cx;
    int16_t  cy;
    int16_t  x1;
    int16_t  y1;
    int16_t  x2;
    int16_t  y2;
    int8_t   vx;
    int8_t   vy;
    uint8_t  gesture_type;
    uint8_t  face_id;
    uint8_t  confidence;
    uint8_t  reserved[11];
} subboard_detection_result_t;

static inline uint8_t subboard_detection_result_get_verify_state(const subboard_detection_result_t *result)
{
    return (result != 0) ? result->reserved[SUBBOARD_DETECTION_RESERVED_VERIFY_STATE_IDX] :
                              SUBBOARD_FACE_VERIFY_STATE_NONE;
}

static inline uint8_t subboard_detection_result_get_verify_score(const subboard_detection_result_t *result)
{
    return (result != 0) ? result->reserved[SUBBOARD_DETECTION_RESERVED_VERIFY_SCORE_IDX] : 0u;
}

static inline uint8_t subboard_detection_result_get_verify_flags(const subboard_detection_result_t *result)
{
    return (result != 0) ? result->reserved[SUBBOARD_DETECTION_RESERVED_VERIFY_FLAGS_IDX] : 0u;
}

static inline void subboard_detection_result_set_verify_state(subboard_detection_result_t *result,
                                                              uint8_t verify_state)
{
    if (result != 0) {
        result->reserved[SUBBOARD_DETECTION_RESERVED_VERIFY_STATE_IDX] = verify_state;
    }
}

static inline void subboard_detection_result_set_verify_score(subboard_detection_result_t *result,
                                                              uint8_t verify_score)
{
    if (result != 0) {
        result->reserved[SUBBOARD_DETECTION_RESERVED_VERIFY_SCORE_IDX] = verify_score;
    }
}

static inline void subboard_detection_result_set_verify_flags(subboard_detection_result_t *result,
                                                              uint8_t verify_flags)
{
    if (result != 0) {
        result->reserved[SUBBOARD_DETECTION_RESERVED_VERIFY_FLAGS_IDX] = verify_flags;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* SUBBOARD_DETECTION_RESULT_H */