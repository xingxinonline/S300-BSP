#ifndef SUBBOARD_DSP_CTRL_H
#define SUBBOARD_DSP_CTRL_H

#include <stdbool.h>
#include <stdint.h>

#include "subboard_detection_result.h"
#include "subboard_tracking_summary.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	SUBBOARD_DSP_TRACKING_IDLE = 0,
	SUBBOARD_DSP_TRACKING_FOLLOWING,
} subboard_dsp_tracking_state_t;

void subboard_dsp_ctrl_init(uint32_t (*get_millis_fn)(void));
void subboard_dsp_ctrl_reset(void);
void subboard_dsp_ctrl_set_mm_ready(bool ready);
void subboard_dsp_ctrl_set_resource_flags(uint8_t resource_flags);
int subboard_dsp_ctrl_start(void);
void subboard_dsp_ctrl_tick(void);
bool subboard_dsp_ctrl_is_active(void);
bool subboard_dsp_ctrl_has_error(void);
uint8_t subboard_dsp_ctrl_get_public_state(void);
uint8_t subboard_dsp_ctrl_peek_master_request(void);
void subboard_dsp_ctrl_complete_master_request(uint8_t request);
bool subboard_dsp_ctrl_get_latest_result(subboard_detection_result_t *out_result);
bool subboard_dsp_ctrl_get_latest_tracking_summary(subboard_tracking_summary_t *out_summary);
void subboard_dsp_ctrl_reset_face_session(void);
uint8_t subboard_dsp_ctrl_send_tracking_command(uint8_t track_opcode);
subboard_dsp_tracking_state_t subboard_dsp_ctrl_get_tracking_state(void);
bool subboard_dsp_ctrl_is_tracking_command_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* SUBBOARD_DSP_CTRL_H */