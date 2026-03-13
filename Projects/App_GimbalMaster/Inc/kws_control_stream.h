#ifndef KWS_CONTROL_STREAM_H
#define KWS_CONTROL_STREAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*kws_control_stream_keyword_callback_t)(uint8_t keyword_idx,
                                                      uint8_t confidence,
                                                      uint32_t chunk_idx);

uint32_t kws_control_stream_get_audio_in_addr(void);
uint32_t kws_control_stream_get_cmd_addr(void);
void kws_control_stream_log_layout(void);
void kws_control_stream_dma0_irq_handler(void);
void kws_control_stream_reset_session(void);
void kws_control_stream_set_running(uint8_t running);
void kws_control_stream_set_keyword_report_callback(kws_control_stream_keyword_callback_t callback);
void kws_control_stream_step(void);

#ifdef __cplusplus
}
#endif

#endif