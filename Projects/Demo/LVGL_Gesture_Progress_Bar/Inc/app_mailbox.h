#ifndef _APP_MAILBOX_H_
#define _APP_MAILBOX_H_

#include <stdint.h>
#include <stdbool.h>
#include "detection_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Init app mailbox logic
 */
void app_mailbox_init(void);

/**
 * @brief Provide a function to get current milliseconds (for timeout logic)
 * @param get_ms Function pointer returning uint32_t ms
 */
void app_mailbox_set_time_callback(uint32_t (*get_ms)(void));

/**
 * @brief Poll mailbox for new face/gesture results. Should be called in main loop.
 */
void app_mailbox_poll(void);

/**
 * @brief Check if face/gesture was detected recently (within timeout).
 * @return true if detected, false if timeout.
 */
bool app_mailbox_is_face_present(void);

/**
 * @brief 获取最近一次识别到的手势类型。
 * @return FACE/PERSON/GESTURE/PALM/PEACE/UNKNOWN 中的一种；无有效手势时返回 UNKNOWN。
 */
DetectionType_t app_mailbox_get_last_gesture_type(void);

#ifdef __cplusplus
}
#endif

#endif
