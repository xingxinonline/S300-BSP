#ifndef HUMAN_TRACKING_APP_H
#define HUMAN_TRACKING_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void human_tracking_app_init(uint32_t (*get_millis_fn)(void));
void human_tracking_app_tick(void);

#ifdef __cplusplus
}
#endif

#endif