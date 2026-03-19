#ifndef HUMAN_DETECTION_OVERLAY_H
#define HUMAN_DETECTION_OVERLAY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void human_detection_overlay_init(uint32_t (*get_millis_fn)(void));
void human_detection_overlay_reset(void);
void human_detection_overlay_tick(void);
bool human_detection_overlay_handle_mailbox_message(uint32_t msg);

#ifdef __cplusplus
}
#endif

#endif