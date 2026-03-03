#ifndef KWS_DSP_H
#define KWS_DSP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t kws_dsp_get_audio_in_addr(void);
uint32_t kws_dsp_get_cmd_addr(void);
void kws_dsp_dma0_irq_handler(void);
void kws_dsp_run_polling(void);

#ifdef __cplusplus
}
#endif

#endif
