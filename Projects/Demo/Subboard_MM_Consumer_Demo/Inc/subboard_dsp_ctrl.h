#ifndef SUBBOARD_DSP_CTRL_H
#define SUBBOARD_DSP_CTRL_H

#include <stdbool.h>
#include <stdint.h>

#include "subboard_detection_result.h"

#ifdef __cplusplus
extern "C" {
#endif

void subboard_dsp_ctrl_init(uint32_t (*get_millis_fn)(void));
void subboard_dsp_ctrl_reset(void);
void subboard_dsp_ctrl_set_mm_ready(bool ready);
int subboard_dsp_ctrl_start(void);
void subboard_dsp_ctrl_tick(void);
bool subboard_dsp_ctrl_is_active(void);
bool subboard_dsp_ctrl_has_error(void);
uint8_t subboard_dsp_ctrl_get_public_state(void);
uint8_t subboard_dsp_ctrl_peek_master_request(void);
void subboard_dsp_ctrl_complete_master_request(uint8_t request);
bool subboard_dsp_ctrl_get_latest_result(subboard_detection_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif /* SUBBOARD_DSP_CTRL_H */