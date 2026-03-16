#ifndef HAND_GESTURE_APP_H
#define HAND_GESTURE_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void hand_gesture_app_init(uint32_t (*get_millis_fn)(void));
void hand_gesture_app_tick(void);

#ifdef __cplusplus
}
#endif

#endif