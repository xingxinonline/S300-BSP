#ifndef SUBBOARD_FACE_RECOGNITION_ADAPTER_H
#define SUBBOARD_FACE_RECOGNITION_ADAPTER_H

#include <stdbool.h>

#include "subboard_detection_result.h"
#include "subboard_tracking_summary.h"

#ifdef __cplusplus
extern "C" {
#endif

void subboard_face_recognition_adapter_reset(void);
void subboard_face_recognition_adapter_clear(void);
void subboard_face_recognition_adapter_reset_session(void);
void subboard_face_recognition_adapter_update_from_multi(const void *result_payload);
bool subboard_face_recognition_adapter_get_latest(subboard_detection_result_t *out_result);
bool subboard_face_recognition_adapter_get_latest_tracking(subboard_tracking_summary_t *out_summary);

#ifdef __cplusplus
}
#endif

#endif