#include "kws_control_stream.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "kws_proto.h"
#include "audio_app.h"
#include "audio_codec.h"
#include "board.h"
#include "dma.h"
#include "i2s.h"
#include "mailbox_proto.h"
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

#ifndef MASTER_KWS_STREAM_STATS_LOG_INTERVAL
#define MASTER_KWS_STREAM_STATS_LOG_INTERVAL 1024u
#endif

#ifndef MASTER_KWS_TOP_SCORE_LOG_INTERVAL
#define MASTER_KWS_TOP_SCORE_LOG_INTERVAL 128u
#endif

#ifndef MASTER_KWS_UNKNOWN_SCORE_LOG_INTERVAL
#define MASTER_KWS_UNKNOWN_SCORE_LOG_INTERVAL MASTER_KWS_STREAM_STATS_LOG_INTERVAL
#endif

#ifndef MASTER_KWS_USE_MIC4
#define MASTER_KWS_USE_MIC4 0u
#endif

#define KWS_REPORT_CONFIRM_FRAMES     2u
#define KWS_STRUCTURED_SCORE_COUNT    8u

static volatile int16_t *g_dsp_in_block = (volatile int16_t *)DSP_AUDIO_IN_ADDR;
static volatile int16_t *g_dsp_out_block = (volatile int16_t *)DSP_AUDIO_OUT_ADDR;
static volatile uint8_t *g_kws_cmd = (volatile uint8_t *)DSP_KWS_CMD_ADDR;
static volatile KWSResult_t *g_kws_result = (volatile KWSResult_t *)DSP_KWS_BASE_ADDR;

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
static uint32_t g_last_structured_frame_id = 0u;
static kws_control_stream_keyword_callback_t g_keyword_callback = 0;

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

static const char *kws_selected_mic_name(void)
{
    return (MASTER_KWS_USE_MIC4 != 0u) ? "MIC4" : "MIC1+MIC2";
}

static void kws_update_mic_source(void)
{
    if (!g_mic_initialized) {
        audio_codec_apply_kws_mic_profile(MASTER_KWS_USE_MIC4 != 0u);
        printf("[KWS-CTRL] MIC source -> %s (GPIO%u=%u)\r\n",
               kws_selected_mic_name(),
               (unsigned)BOARD_MIC_SW_PIN,
               gpio_get_value(BOARD_MIC_SW_PORT, BOARD_MIC_SW_PIN) ? 1u : 0u);
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

    if (g_keyword_callback != 0) {
        g_keyword_callback(max_idx, max_val, g_result_count);
    }

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

static void find_structured_top_keyword(const volatile KWSResult_t *result,
                                        uint8_t *out_idx,
                                        uint8_t *out_score)
{
    uint8_t max_val = 0u;
    uint8_t max_idx = 0u;

    for (uint8_t i = 0u; i < KWS_STRUCTURED_SCORE_COUNT; i++) {
        if (result->scores[i] > max_val) {
            max_val = result->scores[i];
            max_idx = i;
        }
    }

    *out_idx = max_idx;
    *out_score = max_val;
}

static bool should_log_top_keyword_diagnostic(uint8_t max_idx, uint8_t max_val)
{
    if ((MASTER_KWS_TOP_SCORE_LOG_INTERVAL == 0u) ||
        ((g_result_count % MASTER_KWS_TOP_SCORE_LOG_INTERVAL) != 0u)) {
        return false;
    }

    if ((max_idx == 0u) && (max_val == 0u)) {
        if (MASTER_KWS_UNKNOWN_SCORE_LOG_INTERVAL == 0u) {
            return false;
        }

        return (g_result_count % MASTER_KWS_UNKNOWN_SCORE_LOG_INTERVAL) == 0u;
    }

    return true;
}

static void log_structured_keyword_diagnostic(const volatile KWSResult_t *result)
{
    uint8_t max_val = 0u;
    uint8_t max_idx = 0u;
    uint8_t threshold = 0u;

    find_structured_top_keyword(result, &max_idx, &max_val);
    if (!should_log_top_keyword_diagnostic(max_idx, max_val)) {
        return;
    }

    if (max_idx < KEYWORDS_COUNT) {
        threshold = g_keyword_thresholds[max_idx];
        printf("[KWS-CTRL] top1=%s score=%u threshold=%u frame=%lu mic=%s scores=[%u,%u,%u,%u,%u,%u,%u,%u] keyword_idx=%u confidence=%u\r\n",
               g_keywords[max_idx],
               (unsigned)max_val,
               (unsigned)threshold,
               (unsigned long)result->frame_id,
               kws_selected_mic_name(),
               (unsigned)result->scores[0],
               (unsigned)result->scores[1],
               (unsigned)result->scores[2],
               (unsigned)result->scores[3],
               (unsigned)result->scores[4],
               (unsigned)result->scores[5],
               (unsigned)result->scores[6],
               (unsigned)result->scores[7],
               (unsigned)result->keyword_idx,
               (unsigned)result->confidence);
    }
}

static void report_structured_keyword(const volatile KWSResult_t *result)
{
    uint32_t chunk_idx = (result->frame_id != 0u) ? result->frame_id : g_result_count;

    if (g_keyword_callback != 0) {
        g_keyword_callback((uint8_t)result->keyword_idx,
                           (uint8_t)result->confidence,
                           chunk_idx);
    }

    if (result->keyword_idx < KEYWORDS_COUNT) {
        printf("[KWS-CTRL] >>> %s (confidence=%u, frame=%lu, cycles=%lu)\r\n",
               g_keywords[result->keyword_idx],
               (unsigned)result->confidence,
               (unsigned long)chunk_idx,
               (unsigned long)(*g_dsp_calc_cycles));
    } else {
        printf("[KWS-CTRL] >>> keyword[%u] (confidence=%u, frame=%lu, cycles=%lu)\r\n",
               (unsigned)result->keyword_idx,
               (unsigned)result->confidence,
               (unsigned long)chunk_idx,
               (unsigned long)(*g_dsp_calc_cycles));
    }
}

static bool handle_structured_result(const volatile KWSResult_t *result, bool log_invalid)
{
    if (!KWS_RESULT_IS_VALID(result)) {
        if (log_invalid) {
            printf("[KWS-CTRL] structured result invalid @0x%08lX magic=0x%08lX\r\n",
                   (unsigned long)(uintptr_t)result,
                   (unsigned long)result->magic);
        }
        return false;
    }

    if ((result->frame_id != 0u) && (result->frame_id == g_last_structured_frame_id)) {
        return true;
    }

    g_last_structured_frame_id = result->frame_id;
    g_result_count++;
    log_structured_keyword_diagnostic(result);

    if (KWS_IS_VALID_KEYWORD(result->keyword_idx) && (result->confidence != 0u)) {
        report_structured_keyword(result);
    }

    return true;
}

static void log_top_keyword_diagnostic(void)
{
    uint8_t max_val = 0u;
    uint8_t max_idx = 0u;
    uint8_t threshold = 0u;
    unsigned score0;
    unsigned score1;
    unsigned score2;
    unsigned score3;
    unsigned score4;
    unsigned score5;
    unsigned score6;
    unsigned score7;

    for (uint8_t i = 0; i < 8u; i++) {
        if (g_kws_cmd[i] > max_val) {
            max_val = g_kws_cmd[i];
            max_idx = i;
        }
    }

    if (!should_log_top_keyword_diagnostic(max_idx, max_val)) {
        return;
    }

    score0 = (unsigned)g_kws_cmd[0];
    score1 = (unsigned)g_kws_cmd[1];
    score2 = (unsigned)g_kws_cmd[2];
    score3 = (unsigned)g_kws_cmd[3];
    score4 = (unsigned)g_kws_cmd[4];
    score5 = (unsigned)g_kws_cmd[5];
    score6 = (unsigned)g_kws_cmd[6];
    score7 = (unsigned)g_kws_cmd[7];

    if (max_idx < KEYWORDS_COUNT) {
        threshold = g_keyword_thresholds[max_idx];
        printf("[KWS-CTRL] top1=%s score=%u threshold=%u pending_hits=%u chunk=%lu mic=%s scores=[%u,%u,%u,%u,%u,%u,%u,%u]\r\n",
               g_keywords[max_idx],
               (unsigned)max_val,
               (unsigned)threshold,
               (unsigned)g_pending_keyword_hits,
               (unsigned long)g_result_count,
               kws_selected_mic_name(),
               score0,
               score1,
               score2,
               score3,
               score4,
               score5,
               score6,
               score7);
    } else {
        printf("[KWS-CTRL] top1=keyword[%u] score=%u threshold=? pending_hits=%u chunk=%lu mic=%s scores=[%u,%u,%u,%u,%u,%u,%u,%u]\r\n",
               (unsigned)max_idx,
               (unsigned)max_val,
               (unsigned)g_pending_keyword_hits,
               (unsigned long)g_result_count,
               kws_selected_mic_name(),
               score0,
               score1,
               score2,
               score3,
               score4,
               score5,
               score6,
               score7);
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
    g_last_structured_frame_id = 0u;

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

void kws_control_stream_set_keyword_report_callback(kws_control_stream_keyword_callback_t callback)
{
    g_keyword_callback = callback;
}

bool kws_control_stream_handle_mailbox_message(uint32_t msg)
{
    uint32_t msg_type = MAILBOX_GET_MSG_TYPE(msg);
    uint32_t payload = MAILBOX_GET_PAYLOAD(msg);

    if (msg_type == MAILBOX_MSG_TYPE_KWS) {
        uintptr_t addr = (uintptr_t)DSP_KWS_BASE_ADDR + (uintptr_t)payload;
        const volatile KWSResult_t *result = (const volatile KWSResult_t *)addr;

        (void)handle_structured_result(result, true);
        return true;
    }

    if (msg_type == MAILBOX_MSG_TYPE_NO_RESULT) {
        return true;
    }

    return false;
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

        if (!handle_structured_result(g_kws_result, false)) {
            g_result_count++;
            log_top_keyword_diagnostic();
            log_top_keyword();
        }
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

    if ((MASTER_KWS_STREAM_STATS_LOG_INTERVAL != 0u) &&
        ((g_audio_chunk_count % MASTER_KWS_STREAM_STATS_LOG_INTERVAL) == 0u)) {
        printf("[KWS-CTRL] chunks=%lu results=%lu backpressure=%lu out_of=%lu\r\n",
               (unsigned long)g_audio_chunk_count,
               (unsigned long)g_result_count,
               (unsigned long)g_input_backpressure_count,
               (unsigned long)g_out_overflow_count);
    }
}