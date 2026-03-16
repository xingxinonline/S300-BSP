#ifndef HAND_GESTURE_OVERLAY_H
#define HAND_GESTURE_OVERLAY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void hand_gesture_overlay_init(uint32_t (*get_millis_fn)(void));
void hand_gesture_overlay_reset(void);
void hand_gesture_overlay_tick(void);
bool hand_gesture_overlay_handle_mailbox_message(uint32_t msg);

#ifdef __cplusplus
}
#endif

#endif