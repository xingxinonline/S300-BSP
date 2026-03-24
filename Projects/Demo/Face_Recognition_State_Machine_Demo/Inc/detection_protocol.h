/**
 * @file detection_protocol.h
 * @brief DSP与M4之间的人脸检测+识别结果协议定义
 */

#ifndef DETECTION_PROTOCOL_H_
#define DETECTION_PROTOCOL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_DETECTION_COUNT              10u
#define DETECTION_RESULT_MAGIC           0x44455446u
#define DETECTION_PROTOCOL_VERSION       0x0204u
#define FACE_FEATURE_DIMENSION           128u
#define FACE_FEATURE_VECTOR_DIM          FACE_FEATURE_DIMENSION
#define DETECTION_RESULT_FLAG_HAS_FEATURE 0x00000001u
#define DSP_PTCM_M4_BASE_OFFSET          0x44800000u

typedef enum {
    DETECTION_VERIFY_STATE_NONE = 0,
    DETECTION_VERIFY_STATE_WAIT_ANCHOR = 1,
    DETECTION_VERIFY_STATE_MATCH = 2,
    DETECTION_VERIFY_STATE_UNCERTAIN = 3,
    DETECTION_VERIFY_STATE_NO_MATCH = 4,
} DetectionVerifyState_e;

#define DETECTION_CANDIDATE_FLAG_PRESENT         (1u << 0)
#define DETECTION_CANDIDATE_FLAG_ALLOW_EXTRACT   (1u << 1)
#define DETECTION_CANDIDATE_FLAG_ALLOW_UPDATE    (1u << 2)
#define DETECTION_CANDIDATE_FLAG_FEATURE_VALID   (1u << 3)
#define DETECTION_CANDIDATE_FLAG_TEMPLATE_MATCH  (1u << 4)
#define DETECTION_CANDIDATE_FLAG_TEMPLATE_ENROLL (1u << 5)
#define DETECTION_CANDIDATE_FLAG_TEMPLATE_FUSE   (1u << 6)

typedef enum {
    DETECTION_TYPE_UNKNOWN  = 0,
    DETECTION_TYPE_FACE     = 1,
    DETECTION_TYPE_PERSON   = 2,
    DETECTION_TYPE_GESTURE  = 3,
    DETECTION_TYPE_OBJECT   = 4,
} DetectionType_e;

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
} DetectionBox_t;

typedef struct __attribute__((packed)) {
    uint32_t       magic;
    uint32_t       version;
    uint32_t       frame_id;
    uint32_t       timestamp;
    uint32_t       count;
    int32_t        selected_idx;
    DetectionBox_t boxes[MAX_DETECTION_COUNT];
    uint32_t       feature_flags;
    uint32_t       feature_dim;
    int8_t         feature_vector[FACE_FEATURE_DIMENSION];
    uint8_t        verify_state;
    uint8_t        verify_score;
    uint8_t        template_count;
    uint8_t        candidate_confidence;
    uint8_t        candidate_flags;
    uint8_t        reserved[3];
} DetectionResult_t;

#define MAILBOX_MSG_TYPE_SINGLE     0x00000000u
#define MAILBOX_MSG_TYPE_MULTI      0x10000000u
#define MAILBOX_MSG_TYPE_NO_DETECT  0xF0000000u
#define MAILBOX_MSG_TYPE_NO_RESULT  MAILBOX_MSG_TYPE_NO_DETECT
#define MAILBOX_MSG_TYPE_MASK       0xF0000000u
#define MAILBOX_MSG_PAYLOAD_MASK    0x0FFFFFFFu

#define MAKE_MULTI_DETECT_MSG(offset) \
    (MAILBOX_MSG_TYPE_MULTI | ((offset) & MAILBOX_MSG_PAYLOAD_MASK))

#define MAKE_SINGLE_DETECT_MSG(offset) \
    (MAILBOX_MSG_TYPE_SINGLE | ((offset) & MAILBOX_MSG_PAYLOAD_MASK))

#define GET_MSG_TYPE(msg)    ((msg) & MAILBOX_MSG_TYPE_MASK)
#define GET_MSG_PAYLOAD(msg) ((msg) & MAILBOX_MSG_PAYLOAD_MASK)

#ifndef COMPILE_TIME_ASSERT
#define COMPILE_TIME_ASSERT(cond, msg) \
    typedef char static_assertion_##msg[(cond) ? 1 : -1]
#endif

COMPILE_TIME_ASSERT(sizeof(DetectionBox_t) == 68, DetectionBox_size_mismatch);
COMPILE_TIME_ASSERT(sizeof(DetectionResult_t) == 848, DetectionResult_size_mismatch);

#ifdef __cplusplus
}
#endif

#endif