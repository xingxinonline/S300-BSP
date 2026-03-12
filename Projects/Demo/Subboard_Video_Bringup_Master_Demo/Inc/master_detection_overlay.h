#ifndef MASTER_DETECTION_OVERLAY_H
#define MASTER_DETECTION_OVERLAY_H

#include <stdint.h>

#include "subboard_detection_result.h"

#ifdef __cplusplus
extern "C" {
#endif

void master_detection_overlay_init(uint32_t (*get_millis_fn)(void));
void master_detection_overlay_draw(const subboard_detection_result_t *result);
void master_detection_overlay_clear(void);
void master_detection_overlay_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* MASTER_DETECTION_OVERLAY_H */