#ifndef SUBBOARD_DSP_CONTROL_PLANE_H
#define SUBBOARD_DSP_CONTROL_PLANE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SUB_DSP_STATE_IDLE = 0,
    SUB_DSP_STATE_HANDSHAKING,
    SUB_DSP_STATE_CM4_RESOURCE_READY,
    SUB_DSP_STATE_DSP_READY,
    SUB_DSP_STATE_CONFIGURED,
    SUB_DSP_STATE_RUNNING,
    SUB_DSP_STATE_ERROR,
} SubboardDspState_t;

typedef enum {
    SUB_DSP_PENDING_NONE = 0,
    SUB_DSP_PENDING_CONFIG_ACK,
    SUB_DSP_PENDING_BUFFER_ACK,
    SUB_DSP_PENDING_START_ACK,
    SUB_DSP_PENDING_TRACK_START_ACK,
    SUB_DSP_PENDING_TRACK_STOP_ACK,
    SUB_DSP_PENDING_TRACK_RESET_ACK,
} SubboardDspPending_t;

typedef struct {
    bool consumed;
    bool has_next_state;
    bool has_next_pending;
    bool request_resource_ready;
    bool request_config_apply;
    bool request_buffer_bind;
    bool request_start_stream;
    bool request_master_mm_runtime;
    SubboardDspState_t next_state;
    SubboardDspPending_t next_pending;
} SubboardDspControlEffect_t;

const char *subboard_dsp_control_plane_state_name(SubboardDspState_t state);
bool subboard_dsp_control_plane_handle_runtime_message(uint32_t msg,
                                                       SubboardDspState_t state,
                                                       SubboardDspControlEffect_t *out_effect);
void subboard_dsp_control_plane_handle_control_message(uint32_t msg,
                                                       uint8_t session_id,
                                                       bool mm_ready,
                                                       SubboardDspState_t state,
                                                       SubboardDspPending_t pending,
                                                       SubboardDspControlEffect_t *out_effect);

#ifdef __cplusplus
}
#endif

#endif /* SUBBOARD_DSP_CONTROL_PLANE_H */