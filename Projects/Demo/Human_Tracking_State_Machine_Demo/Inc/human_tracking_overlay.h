#ifndef HUMAN_TRACKING_OVERLAY_H
#define HUMAN_TRACKING_OVERLAY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint32_t frame_id;
	uint16_t timestamp_ms;
	uint32_t count;
	int32_t selected_idx;
	uint8_t tracker_state;
	uint8_t tracker_flags;
	uint8_t primary_track_id;
	uint8_t primary_score_pct;
	uint8_t primary_miss_count;
	bool has_target;
} human_tracking_overlay_status_t;

void human_tracking_overlay_init(uint32_t (*get_millis_fn)(void));
void human_tracking_overlay_reset(void);
void human_tracking_overlay_tick(void);
bool human_tracking_overlay_handle_mailbox_message(uint32_t msg);
void human_tracking_overlay_get_status(human_tracking_overlay_status_t *out_status);
void human_tracking_overlay_set_tracking_active(bool tracking_active);

#ifdef __cplusplus
}
#endif

#endif