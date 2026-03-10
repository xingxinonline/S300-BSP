#include "kws_control_stream.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "audio_app.h"
#include "audio_codec.h"
#include "dma.h"
#include "i2s.h"
#include "s300.h"

#define DSP_AUDIO_IN_ADDR             0x44040000u
#define DSP_AUDIO_OUT_ADDR            0x44040200u
#define DSP_KWS_CMD_ADDR              0x44040400u

#define DSP_READY_FLAG_ADDR           0x44040430u
#define DSP_INPUT_READY_FLAG_ADDR     0x44040434u
#define DSP_OUTPUT_READY_FLAG_ADDR    0x44040438u
#define DSP_CALC_CYCLES_ADDR          0x4404043Cu

#define AUDIO_BLOCK_LEN               (AUDIO_DMA_BUFFER_LEN / 2)
#define KWS_DSP_NOT_READY_LOG_DIV     2048u
#define KWS_STATS_LOG_INTERVAL        256u
#define KWS_REPORT_CONFIRM_FRAMES     2u

static volatile int16_t *g_dsp_in_block = (volatile int16_t *)DSP_AUDIO_IN_ADDR;
static volatile int16_t *g_dsp_out_block = (volatile int16_t *)DSP_AUDIO_OUT_ADDR;
static volatile uint8_t *g_kws_cmd = (volatile uint8_t *)DSP_KWS_CMD_ADDR;

static volatile uint32_t *g_dsp_ready_flag = (volatile uint32_t *)DSP_READY_FLAG_ADDR;
static volatile uint32_t *g_input_ready_flag = (volatile uint32_t *)DSP_INPUT_READY_FLAG_ADDR;
static volatile uint32_t *g_output_ready_flag = (volatile uint32_t *)DSP_OUTPUT_READY_FLAG_ADDR;
static volatile uint32_t *g_dsp_calc_cycles = (volatile uint32_t *)DSP_CALC_CYCLES_ADDR;

static int16_t g_audio_block_right[AUDIO_BLOCK_LEN];

static uint8_t g_stream_running = 0u;
static uint32_t g_audio_chunk_count = 0u;
static uint32_t g_result_count = 0u;
static uint32_t g_not_ready_count = 0u;
static uint32_t g_input_backpressure_count = 0u;
static uint32_t g_out_overflow_count = 0u;
static uint32_t g_keyword_last_report_chunk[8] = {0u};
static uint8_t g_pending_keyword_idx = 0u;
static uint8_t g_pending_keyword_hits = 0u;
static uint32_t g_pending_keyword_chunk = 0u;

static const char *g_keywords[] = {
    "unknown",
    "pai-zhang-zhaopian",
    "kaishi-luxiang",
    "tingzhi-luxiang",
    "qidong-gensui",
    "jieshu-gensui",
    "dakai-buguangdeng",
    "guanbi-buguangdeng",
};

static const uint8_t g_keyword_thresholds[] = {
    255u,
    96u,
    94u,
    94u,
    96u,
    96u,
    98u,
    98u,
};

static const uint16_t g_keyword_report_cooldown_chunks[] = {
    0u,
    20u,
    16u,
    16u,
    20u,
    20u,
    18u,
    18u,
};

#define KEYWORDS_COUNT  (sizeof(g_keywords) / sizeof(g_keywords[0]))

#if defined(BOARD_AUDIO_CODEC_ES7210)
static bool g_mic_initialized = false;

static void kws_update_mic_source(void)
{
    if (!g_mic_initialized) {
        audio_codec_apply_kws_mic_profile(false);
        printf("[KWS-CTRL] MIC source -> MIC1+MIC2 (fixed)\r\n");
        g_mic_initialized = true;
    }
}
#else
static void kws_update_mic_source(void)
{
}
#endif

static void deinterleave(const int16_t *src, int16_t *dst_left, int16_t *dst_right, int16_t frame_num)
{
    for (int i = 0; i < frame_num; i++) {
        *dst_right++ = *src++;
        *dst_left++ = *src++;
    }
}

static void interleave(const int16_t *src_left, const int16_t *src_right, int16_t *dst, int16_t frame_num)
{
    for (int i = 0; i < frame_num; i++) {
        *dst++ = *src_right++;
        *dst++ = *src_left++;
    }
}

static void log_top_keyword(void)
{
    uint8_t max_val = 0u;
    uint8_t max_idx = 0u;
    uint8_t threshold;
    uint32_t chunk_delta;

    for (uint8_t i = 0; i < 8u; i++) {
        if (g_kws_cmd[i] > max_val) {
            max_val = g_kws_cmd[i];
            max_idx = i;
        }
    }

    if (max_idx == 0u) {
        g_pending_keyword_idx = 0u;
        g_pending_keyword_hits = 0u;
        g_pending_keyword_chunk = 0u;
        return;
    }

    threshold = g_keyword_thresholds[max_idx];
    if (max_val < threshold) {
        g_pending_keyword_idx = 0u;
        g_pending_keyword_hits = 0u;
        g_pending_keyword_chunk = 0u;
        return;
    }

    if ((g_pending_keyword_idx == max_idx) &&
        (g_pending_keyword_chunk + 1u == g_result_count)) {
        if (g_pending_keyword_hits < 0xFFu) {
            g_pending_keyword_hits++;
        }
    } else {
        g_pending_keyword_idx = max_idx;
        g_pending_keyword_hits = 1u;
    }
    g_pending_keyword_chunk = g_result_count;

    if (g_pending_keyword_hits < KWS_REPORT_CONFIRM_FRAMES) {
        return;
    }

    chunk_delta = g_result_count - g_keyword_last_report_chunk[max_idx];
    if ((g_keyword_last_report_chunk[max_idx] != 0u) &&
        (chunk_delta <= g_keyword_report_cooldown_chunks[max_idx])) {
        return;
    }

    g_keyword_last_report_chunk[max_idx] = g_result_count;

    if (max_idx < KEYWORDS_COUNT) {
        printf("[KWS-CTRL] >>> %s (confidence=%u, threshold=%u, chunk=%lu, cycles=%lu)\r\n",
               g_keywords[max_idx],
               (unsigned)max_val,
               (unsigned)threshold,
               (unsigned long)g_result_count,
               (unsigned long)(*g_dsp_calc_cycles));
    } else {
        printf("[KWS-CTRL] >>> keyword[%u] (confidence=%u, threshold=%u, chunk=%lu)\r\n",
               (unsigned)max_idx,
               (unsigned)max_val,
               (unsigned)threshold,
               (unsigned long)g_result_count);
    }
}

uint32_t kws_control_stream_get_audio_in_addr(void)
{
    return DSP_AUDIO_IN_ADDR;
}

uint32_t kws_control_stream_get_cmd_addr(void)
{
    return DSP_KWS_CMD_ADDR;
}

void kws_control_stream_log_layout(void)
{
    printf("[KWS-CTRL] Shared memory layout:\r\n");
    printf("[KWS-CTRL]   audio_in_addr   = 0x%08lX\r\n", (unsigned long)DSP_AUDIO_IN_ADDR);
    printf("[KWS-CTRL]   audio_out_addr  = 0x%08lX\r\n", (unsigned long)DSP_AUDIO_OUT_ADDR);
    printf("[KWS-CTRL]   kws_cmd_addr    = 0x%08lX\r\n", (unsigned long)DSP_KWS_CMD_ADDR);
    printf("[KWS-CTRL]   dsp_ready_flag  = 0x%08lX\r\n", (unsigned long)DSP_READY_FLAG_ADDR);
    printf("[KWS-CTRL]   input_ready     = 0x%08lX\r\n", (unsigned long)DSP_INPUT_READY_FLAG_ADDR);
    printf("[KWS-CTRL]   output_ready    = 0x%08lX\r\n", (unsigned long)DSP_OUTPUT_READY_FLAG_ADDR);
}

void kws_control_stream_dma0_irq_handler(void)
{
    audio_dma0_irq_handler();
}

void kws_control_stream_reset_session(void)
{
    g_stream_running = 0u;
    g_audio_chunk_count = 0u;
    g_result_count = 0u;
    g_not_ready_count = 0u;
    g_input_backpressure_count = 0u;
    g_out_overflow_count = 0u;
    g_pending_keyword_idx = 0u;
    g_pending_keyword_hits = 0u;
    g_pending_keyword_chunk = 0u;

    for (uint32_t i = 0u; i < 8u; i++) {
        g_keyword_last_report_chunk[i] = 0u;
    }

    *g_input_ready_flag = 0u;
    __DSB();
    *g_output_ready_flag = 0u;
    __DSB();

    (void)g_dsp_out_block;
}

void kws_control_stream_set_running(uint8_t running)
{
    g_stream_running = running;
    if (running != 0u) {
        printf("[KWS-CTRL] Data plane enabled, waiting for dsp_ready_flag\r\n");
    }
}

void kws_control_stream_step(void)
{
    audio_ctx_t *ctx;
    int16_t *ptr;

    if (g_stream_running == 0u) {
        return;
    }

    kws_update_mic_source();

    if (*g_output_ready_flag != 0u) {
        __DSB();
        *g_output_ready_flag = 0u;
        __DSB();
        g_result_count++;
        log_top_keyword();
    }

    if (*g_dsp_ready_flag == 0u) {
        g_not_ready_count++;
        if ((g_not_ready_count % KWS_DSP_NOT_READY_LOG_DIV) == 1u) {
            printf("[KWS-CTRL] DSP data plane not ready yet, cnt=%lu\r\n",
                   (unsigned long)g_not_ready_count);
        }
        return;
    }

    if (*g_input_ready_flag != 0u) {
        g_input_backpressure_count++;
        return;
    }

    ctx = audio_get_ctx();
    if (ctx->in_valid_num <= 0) {
        return;
    }

    ptr = ctx->in_buf + ctx->in_read_idx * AUDIO_DMA_BUFFER_LEN;
    deinterleave(ptr, (int16_t *)g_dsp_in_block, g_audio_block_right, AUDIO_BLOCK_LEN);
    __DSB();

    ctx->in_read_idx = (ctx->in_read_idx + 1) % AUDIO_DMA_BUFFER_COUNT;
    ctx->in_valid_num--;

    ptr = ctx->out_buf + ctx->out_write_idx * AUDIO_DMA_BUFFER_LEN;
    interleave((const int16_t *)g_dsp_in_block, (const int16_t *)g_dsp_in_block, ptr, AUDIO_BLOCK_LEN);

    ctx->out_write_idx = (ctx->out_write_idx + 1) % AUDIO_DMA_BUFFER_COUNT;
    if (ctx->out_valid_num < AUDIO_DMA_BUFFER_COUNT) {
        ctx->out_valid_num++;
    } else {
        g_out_overflow_count++;
    }

    *g_input_ready_flag = 1u;
    __DSB();
    g_audio_chunk_count++;

    if ((g_audio_chunk_count % KWS_STATS_LOG_INTERVAL) == 0u) {
        printf("[KWS-CTRL] chunks=%lu results=%lu backpressure=%lu out_of=%lu\r\n",
               (unsigned long)g_audio_chunk_count,
               (unsigned long)g_result_count,
               (unsigned long)g_input_backpressure_count,
               (unsigned long)g_out_overflow_count);
    }
}