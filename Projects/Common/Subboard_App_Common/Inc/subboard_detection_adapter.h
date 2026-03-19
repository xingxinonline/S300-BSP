#ifndef SUBBOARD_DETECTION_ADAPTER_H
#define SUBBOARD_DETECTION_ADAPTER_H

#include <stdbool.h>

#include "detection_proto.h"
#include "subboard_detection_result.h"
#include "subboard_tracking_summary.h"

#ifdef __cplusplus
extern "C" {
#endif

void subboard_detection_adapter_reset(void);
void subboard_detection_adapter_clear(void);
void subboard_detection_adapter_update_from_multi(const DetectionResult_t *result);
void subboard_detection_adapter_update_from_single(const DetectionBox_t *box);
bool subboard_detection_adapter_get_latest(subboard_detection_result_t *out_result);
bool subboard_detection_adapter_get_latest_tracking(subboard_tracking_summary_t *out_summary);

#ifdef __cplusplus
}
#endif

#endif /* SUBBOARD_DETECTION_ADAPTER_H */