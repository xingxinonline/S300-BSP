#ifndef HUMAN_DETECTION_APP_H
#define HUMAN_DETECTION_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void human_detection_app_init(uint32_t (*get_millis_fn)(void));
void human_detection_app_tick(void);

#ifdef __cplusplus
}
#endif

#endif