#ifndef SUBBOARD_DSP_MAILBOX_H
#define SUBBOARD_DSP_MAILBOX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void subboard_dsp_mailbox_prepare(void);
int subboard_dsp_mailbox_read(uint32_t *out_msg);
void subboard_dsp_mailbox_send_hello(uint8_t session_id);
void subboard_dsp_mailbox_send_resource_ready(uint8_t session_id, uint8_t resource_flags);
void subboard_dsp_mailbox_send_config_apply(uint8_t session_id);
void subboard_dsp_mailbox_send_buffer_bind(uint8_t session_id);
void subboard_dsp_mailbox_send_start_stream(uint8_t session_id);
void subboard_dsp_mailbox_send_heartbeat(uint8_t session_id, uint8_t heartbeat_seq);

#ifdef __cplusplus
}
#endif

#endif /* SUBBOARD_DSP_MAILBOX_H */