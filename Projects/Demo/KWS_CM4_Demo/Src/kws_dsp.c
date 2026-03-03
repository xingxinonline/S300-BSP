#include "kws_dsp.h"
#include "s300.h"
#include "audio_app.h"
#include "audio_codec.h"
#include "gpio.h"
#include "dma.h"
#include "i2s.h"
#include <stdio.h>
#include <stdbool.h>

#define DSP_AUDIO_IN_ADDR               0x44040000u
#define DSP_AUDIO_OUT_ADDR              0x44040200u
#define DSP_KWS_CMD_ADDR                0x44040400u

#define DSP_READY_FLAG_ADDR             0x44040430u
#define DSP_INPUT_READY_FLAG_ADDR       0x44040434u
#define DSP_OUTPUT_READY_FLAG_ADDR      0x44040438u
#define DSP_CALC_CYCLES_ADDR            0x4404043Cu

#define KWS_CONSECUTIVE_TRIGGER_COUNT   3u
#define KWS_SCORE_THRESHOLD             85u
#define KWS_TAKE_ACTION_REPEAT_COUNT    3u
#define KWS_TAKE_ACTION_KEYWORD_IDX     1u

#define DSP_READY_WAIT_LOOPS            20000000u
#define DSP_OUTPUT_WAIT_LOOPS           600000u
#define KWS_WARN_LOG_INTERVAL_FRAMES    128u
#define KWS_STATS_LOG_INTERVAL_FRAMES   320u

static volatile int16_t *dsp_in_block = (volatile int16_t *)DSP_AUDIO_IN_ADDR;
static volatile int16_t *dsp_out_block = (volatile int16_t *)DSP_AUDIO_OUT_ADDR;
static volatile uint8_t *kws_cmd = (volatile uint8_t *)DSP_KWS_CMD_ADDR;

static volatile uint32_t *dsp_ready_flag = (volatile uint32_t *)DSP_READY_FLAG_ADDR;
static volatile uint32_t *input_ready_flag = (volatile uint32_t *)DSP_INPUT_READY_FLAG_ADDR;
static volatile uint32_t *output_ready_flag = (volatile uint32_t *)DSP_OUTPUT_READY_FLAG_ADDR;
static volatile uint32_t *dsp_calc_cycles = (volatile uint32_t *)DSP_CALC_CYCLES_ADDR;

static int16_t audio_block_right[AUDIO_BLOCK_LEN];

static volatile uint32_t i2s_in_overflow_cnt;
static volatile uint32_t i2s_out_overflow_cnt;
static volatile uint32_t i2s_out_underrun_cnt;
static volatile uint32_t dsp_ready_timeout_cnt;
static volatile uint32_t dsp_output_timeout_cnt;
static volatile uint32_t kws_frame_cnt;

static const char *keywords[] = {
    "unknown",
    "paizhangzhaopian",
    "kaishiluxiang",
    "tingzhiluxiang",
    "qidonggensui",
    "jieshugensui",
    "dakaibuguangdeng",
    "guanbibuguangdeng",
};

#define KEYWORDS_COUNT  (sizeof(keywords) / sizeof(keywords[0]))

static uint16_t consecutive_count;
static uint16_t target_idx_val;
static uint8_t counting_started;
static uint8_t print_action_count;
static uint16_t last_print_idx;

#if defined(BOARD_AUDIO_CODEC_ES7210)
static bool g_mic_initialized = false;

static void kws_update_mic_source(void)
{
    /* GPIO31悬空时读取不稳定，禁用动态切换，锁定MIC1+2 */
    if (!g_mic_initialized)
    {
        audio_codec_apply_kws_mic_profile(false);  /* MIC1+MIC2 */
        printf("[KWS] MIC source -> MIC1+MIC2 (fixed)\r\n");
        g_mic_initialized = true;
    }
    /* 不再检测GPIO31变化 */
}
#else
static void kws_update_mic_source(void)
{
}
#endif

uint32_t kws_dsp_get_audio_in_addr(void)
{
    return DSP_AUDIO_IN_ADDR;
}

uint32_t kws_dsp_get_cmd_addr(void)
{
    return DSP_KWS_CMD_ADDR;
}

static uint8_t wait_flag_set(volatile uint32_t *flag, uint32_t wait_loops)
{
    uint32_t loop = 0;

    while ((*flag == 0u) && (loop < wait_loops))
    {
        loop++;
    }

    return (*flag != 0u) ? 1u : 0u;
}

static void take_action(uint16_t keyword_idx, uint8_t confidence)
{
    if (keyword_idx >= KEYWORDS_COUNT)
    {
        printf("[TAKE ACTION] invalid keyword idx: %u, confidence: %u\r\n", keyword_idx, confidence);
        return;
    }

    printf("[TAKE ACTION] execute action - keyword: %s, confidence: %u\r\n",
           keywords[keyword_idx],
           confidence);
}

static void print_scores(const uint8_t *output)
{
    for (uint16_t i = 0; i < KEYWORDS_COUNT; i++)
    {
        if (i == 0u)
        {
            printf("%u", output[i]);
        }
        else
        {
            printf(", %u", output[i]);
        }
    }
    printf("\r\n");
}

static uint16_t kws_show_res(const uint8_t *output)
{
    uint8_t max_val = 0;
    uint16_t idx_val = 0;

    for (uint16_t i = 0; i < KEYWORDS_COUNT; i++)
    {
        uint8_t value = output[i];
        if (max_val < value)
        {
            max_val = value;
            idx_val = i;
        }
    }

    if (!counting_started || idx_val == target_idx_val)
    {
        if (!counting_started)
        {
            target_idx_val = idx_val;
            counting_started = 1u;
        }
        consecutive_count++;

        if (consecutive_count >= KWS_CONSECUTIVE_TRIGGER_COUNT && target_idx_val != 0u)
        {
            consecutive_count = 0u;
            counting_started = 0u;
            print_scores(output);

            if (max_val >= KWS_SCORE_THRESHOLD)
            {
                if (idx_val < KEYWORDS_COUNT)
                {
                    printf("%s:%u\r\n", keywords[idx_val], max_val);

                    if (idx_val == last_print_idx)
                    {
                        print_action_count++;
                    }
                    else
                    {
                        last_print_idx = idx_val;
                        print_action_count = 1u;
                    }

                    if (print_action_count > KWS_TAKE_ACTION_REPEAT_COUNT &&
                        idx_val == KWS_TAKE_ACTION_KEYWORD_IDX)
                    {
                        take_action(idx_val, max_val);
                        print_action_count = 0u;
                    }
                }
                else
                {
                    printf("unknown:%u\r\n", max_val);
                }
            }
        }
    }
    else
    {
        consecutive_count = 1u;
        target_idx_val = idx_val;
    }

    return idx_val;
}

static void kws_debug_top1(const uint8_t *output)
{
    uint8_t max_val = 0u;
    uint16_t idx_val = 0u;

    for (uint16_t i = 0; i < KEYWORDS_COUNT; i++)
    {
        if (output[i] > max_val)
        {
            max_val = output[i];
            idx_val = i;
        }
    }

    if ((kws_frame_cnt % 256u) == 0u)
    {
        printf("[KWS] top1=%u(%s) conf=%u\r\n",
               idx_val,
               (idx_val < KEYWORDS_COUNT) ? keywords[idx_val] : "invalid",
               max_val);
    }
}

void kws_dsp_dma0_irq_handler(void)
{
    audio_ctx_t *ctx = audio_get_ctx();
    S300_DMA_TypeDef *D = DMAC0;

    D->ClearTfr = 0xFFu;
    D->ClearBlock = 0xFFu;
    D->ClearSrcTran = 0xFFu;
    D->ClearDstTran = 0xFFu;
    D->ClearErr = 0xFFu;

    if (is_dma_busy(EM_DMA0, 0) == 0)
    {
        if (ctx->in_valid_num < AUDIO_DMA_BUFFER_COUNT)
        {
            ctx->in_valid_num++;
        }
        else
        {
            i2s_in_overflow_cnt++;
        }

        ctx->in_write_idx = (ctx->in_write_idx + 1) % AUDIO_DMA_BUFFER_COUNT;
        set_dma_std_address(EM_DMA0,
                            0,
                            (uint32_t)&I2S1->RXDMA,
                            (uint32_t)(ctx->in_buf + ctx->in_write_idx * AUDIO_DMA_BUFFER_LEN));
        set_dma_start(EM_DMA0, 0);
    }

    if (is_dma_busy(EM_DMA0, 1) == 0)
    {
        if (ctx->out_valid_num > 0)
        {
            ctx->out_valid_num--;
        }
        else
        {
            i2s_out_underrun_cnt++;
        }

        ctx->out_read_idx = (ctx->out_read_idx + 1) % AUDIO_DMA_BUFFER_COUNT;
        set_dma_std_address(EM_DMA0,
                            1,
                            (uint32_t)(ctx->out_buf + ctx->out_read_idx * AUDIO_DMA_BUFFER_LEN),
                            (uint32_t)&I2S1->TXDMA);
        set_dma_start(EM_DMA0, 1);
    }
}

void kws_dsp_run_polling(void)
{
    audio_ctx_t *ctx = audio_get_ctx();
    int16_t *ptr;
    uint32_t warn_log_div = 0;
    uint32_t stats_log_div = 0;

    ctx->in_read_idx = 0;
    ctx->in_write_idx = 0;
    ctx->in_valid_num = 0;
    ctx->out_read_idx = 0;
    ctx->out_write_idx = 0;
    ctx->out_valid_num = AUDIO_DMA_BUFFER_COUNT;

    *dsp_ready_flag = 0u;
    *input_ready_flag = 0u;
    *output_ready_flag = 0u;

    printf("[KWS] Waiting DSP ready...\r\n");
    while (wait_flag_set(dsp_ready_flag, DSP_READY_WAIT_LOOPS) == 0u)
    {
        dsp_ready_timeout_cnt++;
        warn_log_div++;
        if ((warn_log_div % KWS_WARN_LOG_INTERVAL_FRAMES) == 1u)
        {
            printf("[KWS] wait dsp ready timeout, cnt=%lu\r\n", (unsigned long)dsp_ready_timeout_cnt);
        }
    }
    printf("[KWS] DSP ready\r\n");

    while (1)
    {
        kws_update_mic_source();

        if (ctx->in_valid_num <= 0)
        {
            continue;
        }

        ptr = ctx->in_buf + ctx->in_read_idx * AUDIO_DMA_BUFFER_LEN;
        audio_deinterleave(ptr, (int16_t *)dsp_in_block, audio_block_right, AUDIO_BLOCK_LEN);

        ctx->in_read_idx = (ctx->in_read_idx + 1) % AUDIO_DMA_BUFFER_COUNT;
        ctx->in_valid_num--;

        ptr = ctx->out_buf + ctx->out_write_idx * AUDIO_DMA_BUFFER_LEN;
        audio_interleave((const int16_t *)dsp_in_block, (const int16_t *)dsp_in_block, ptr, AUDIO_BLOCK_LEN);

        ctx->out_write_idx = (ctx->out_write_idx + 1) % AUDIO_DMA_BUFFER_COUNT;
        if (ctx->out_valid_num < AUDIO_DMA_BUFFER_COUNT)
        {
            ctx->out_valid_num++;
        }
        else
        {
            i2s_out_overflow_cnt++;
        }

        *input_ready_flag = 1u;
        if (wait_flag_set(output_ready_flag, DSP_OUTPUT_WAIT_LOOPS) == 0u)
        {
            dsp_output_timeout_cnt++;
            *input_ready_flag = 0u;

            warn_log_div++;
            if ((warn_log_div % KWS_WARN_LOG_INTERVAL_FRAMES) == 1u)
            {
                printf("[KWS] wait dsp output timeout, cnt=%lu\r\n",
                       (unsigned long)dsp_output_timeout_cnt);
            }
            continue;
        }

        *output_ready_flag = 0u;
        kws_frame_cnt++;
        kws_debug_top1((const uint8_t *)kws_cmd);
        kws_show_res((const uint8_t *)kws_cmd);

        stats_log_div++;
        if ((stats_log_div % KWS_STATS_LOG_INTERVAL_FRAMES) == 1u)
        {
            printf("[KWS] stats in_of=%lu out_of=%lu out_ud=%lu dsp_to=%lu\r\n",
                   (unsigned long)i2s_in_overflow_cnt,
                   (unsigned long)i2s_out_overflow_cnt,
                   (unsigned long)i2s_out_underrun_cnt,
                   (unsigned long)dsp_output_timeout_cnt);
        }

        (void)dsp_out_block;
        (void)dsp_calc_cycles;
    }
}
