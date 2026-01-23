/**
 * @file    audio_app.c
 * @brief   音频应用层实现 - I2S DMA 双缓冲 + WM8978 Codec
 * @note    移植自 gitlab/feat/kws:test_kcx_audio/cortex-m4-kcx/app/main.c
 *          适配到 S300-BSP SDK 架构
 */

#include "audio_app.h"
#include "s300.h"
#include "board.h"
#include "rcc.h"
#include "gpio.h"
#include "dma.h"
#include "i2s.h"
#include "i2c_soft.h"
#include "wm8978.h"
#include <stdio.h>
#include <string.h>

/*===========================================================================
 * 板级引脚配置 (与原 demo 对齐，可在 board.h 中覆盖)
 *===========================================================================*/

/* I2S1 引脚: PA3=MCLK, PA6=BCLK, PA7=LRCLK, PA8=DO, PA9=DI */
#ifndef BOARD_I2S_PORT
#define BOARD_I2S_PORT              GPIOA
#endif

#ifndef BOARD_I2S_MCLK_PIN
#define BOARD_I2S_MCLK_PIN          3
#endif

#ifndef BOARD_I2S_BCLK_PIN
#define BOARD_I2S_BCLK_PIN          6
#endif

#ifndef BOARD_I2S_LRCLK_PIN
#define BOARD_I2S_LRCLK_PIN         7
#endif

#ifndef BOARD_I2S_DO_PIN
#define BOARD_I2S_DO_PIN            8
#endif

#ifndef BOARD_I2S_DI_PIN
#define BOARD_I2S_DI_PIN            9
#endif

#ifndef BOARD_I2S_MCLK_FUNCTION
#define BOARD_I2S_MCLK_FUNCTION     FUNCTION_1   /* PA3 MCLK 使用 FUNCTION_1 */
#endif

#ifndef BOARD_I2S_FUNCTION
#define BOARD_I2S_FUNCTION          FUNCTION_3   /* PA6-9 使用 FUNCTION_3 */
#endif

/* WM8978 软件 I2C 配置:
 * 使用 SDK 的 i2c_soft_init_default_idx() 便捷 API，索引 3 对应:
 *   - PA4 = SCL
 *   - PA5 = SDA
 * 如需自定义引脚，请改用 i2c_soft_init() + i2c_soft_cfg_t
 */

/*===========================================================================
 * 全局变量
 *===========================================================================*/

/* 音频上下文 */
static audio_ctx_t g_audio_ctx;

/* WM8978 I2C 句柄 */
static i2c_soft_t g_wm8978_i2c;

/*===========================================================================
 * 内部辅助函数
 *===========================================================================*/

/**
 * @brief 配置 I2S 引脚
 * @note  参考 gitlab/feature/i2s-kws-audio-processing:
 *        PA3(MCLK) 使用 FUNCTION_1
 *        PA6-9 使用 FUNCTION_3
 */
static void audio_i2s_pins_init(void)
{
    const uint8_t pins[] = {
        BOARD_I2S_MCLK_PIN,
        BOARD_I2S_BCLK_PIN,
        BOARD_I2S_LRCLK_PIN,
        BOARD_I2S_DO_PIN,
        BOARD_I2S_DI_PIN
    };
    const gpio_func_t funcs[] = {
        BOARD_I2S_MCLK_FUNCTION,  /* PA3 MCLK -> FUNCTION_1 */
        BOARD_I2S_FUNCTION,       /* PA6 BCLK -> FUNCTION_3 */
        BOARD_I2S_FUNCTION,       /* PA7 LRCLK -> FUNCTION_3 */
        BOARD_I2S_FUNCTION,       /* PA8 DO -> FUNCTION_3 */
        BOARD_I2S_FUNCTION        /* PA9 DI -> FUNCTION_3 */
    };
    const uint8_t is_out[] = { 1, 1, 1, 1, 0 }; /* MCLK/BCLK/LRCK/DO=out, DI=in */

    for (int i = 0; i < 5; i++)
    {
        gpio_set_function(BOARD_I2S_PORT, pins[i], funcs[i]);
        gpio_set_direction(BOARD_I2S_PORT, pins[i], is_out[i]);
        gpio_set_mode(BOARD_I2S_PORT, pins[i], GPIO_DOWN);
    }
}

/**
 * @brief 初始化 WM8978 软件 I2C
 *
 * I2C 软件模拟索引配置 (来自 i2c_soft.c):
 *   idx=0: PA14(SCL), PA15(SDA)
 *   idx=1: PA0(SCL),  PA1(SDA)  - 原始 gitlab/feature/i2s-kws-audio-processing 使用 EM_I2C1
 *   idx=2: PA2(SCL),  PA3(SDA)
 *   idx=3: PA4(SCL),  PA5(SDA)
 */
static void audio_wm8978_i2c_init(void)
{
    /* 使用 idx=1: PA0(SCL), PA1(SDA)，与原始代码 EM_I2C1 一致 */
    printf("[Audio] I2C init: PA0(SCL), PA1(SDA), 100kHz\r\n");
    i2c_soft_init_default_idx(&g_wm8978_i2c, 1, 100000);
}

/**
 * @brief 配置 WM8978 Codec
 */
static int audio_wm8978_config(void)
{
    int ret;

    /* 初始化 WM8978 */
    ret = wm8978_init(&g_wm8978_i2c);
    if (ret != 0)
    {
        printf("[Audio] WM8978 init failed: %d\r\n", ret);
        printf("[Audio] Check WM8978 connection: PA0(SCL), PA1(SDA)\r\n");
        return ret;
    }

    /* 配置音量 (与原 demo 对齐) */
    wm8978_set_hp_vol(&g_wm8978_i2c, 40, 40);   /* 耳机音量 */
    wm8978_set_spk_vol(&g_wm8978_i2c, 50);      /* 扬声器音量 */

    /* 配置 ADC/DAC */
    wm8978_set_adda(&g_wm8978_i2c, true, true); /* 开启 ADC + DAC */

    /* 配置输入: MIC + LINE IN */
    wm8978_set_input(&g_wm8978_i2c, true, true, false);

    /* 配置输出: DAC 到输出 */
    wm8978_set_output(&g_wm8978_i2c, true, false);

    /* 配置 MIC 增益 */
    wm8978_set_mic_gain(&g_wm8978_i2c, 46);

    /* 配置 I2S 格式: fmt=2 (I2S 标准), len=0 (16-bit) */
    wm8978_i2s_cfg(&g_wm8978_i2c, 2, 0);

    printf("[Audio] WM8978 configured\r\n");
    return 0;
}

/*===========================================================================
 * 公开 API 实现
 *===========================================================================*/

audio_ctx_t *audio_get_ctx(void)
{
    return &g_audio_ctx;
}

void audio_interleave(const int16_t *src_l, const int16_t *src_r, int16_t *dst, uint16_t frame_num)
{
    for (int i = 0; i < frame_num; i++)
    {
        *dst++ = src_r[i];
        *dst++ = src_l[i];
    }
}

void audio_deinterleave(const int16_t *src, int16_t *dst_l, int16_t *dst_r, uint16_t frame_num)
{
    for (int i = 0; i < frame_num; i++)
    {
        *dst_r++ = *src++;
        *dst_l++ = *src++;
    }
}

int audio_init(void)
{
    int ret;

    printf("[Audio] Initializing...\r\n");

    /* 清零上下文 */
    memset(&g_audio_ctx, 0, sizeof(g_audio_ctx));
    g_audio_ctx.out_valid_num = AUDIO_DMA_BUFFER_COUNT; /* 预填充输出缓冲 */

    /* 1. 配置 I2S 引脚 */
    audio_i2s_pins_init();
    printf("[Audio] I2S pins initialized\r\n");

    /* 2. 初始化 WM8978 I2C 并配置 Codec */
    audio_wm8978_i2c_init();
    ret = audio_wm8978_config();
    if (ret != 0)
    {
        return ret;
    }

    /* 3. 配置 Audio PLL (与原 demo 对齐: 12MHz MCLK 输出)
     *    init_audio_pll(3, 129, 500000, 7, 6) 产生约 12MHz
     */
    ret = rcc_init_audio_pll(3, 129, 500000, 7, 6);
    if (ret != 0)
    {
        printf("[Audio] Audio PLL init failed: %d\r\n", ret);
        return ret;
    }

    /* 4. 使能 I2S 时钟 */
    rcc_set_audio_clock(I2S_IDX1, true);

    /* 5. 配置 I2S 基本参数 (MCLK=12MHz, 16-bit) */
    i2s_basic_init(I2S_IDX1, 12000000, I2S_WORD_16);

    /* 6. 使能 I2S */
    i2s_enable(I2S_IDX1, true);

    /* 7. 使能 DMA0 中断 */
    NVIC_EnableIRQ(DMA0_IRQn);

    /* 8. 配置 I2S DMA 模式 (通道 0=录音, 通道 1=播放) */
    i2s_dma_mode(I2S_IDX1, 0, 1,
                 (uint8_t *)g_audio_ctx.in_buf,
                 (uint8_t *)g_audio_ctx.out_buf,
                 AUDIO_DMA_BUFFER_LEN * 2);

    printf("[Audio] Initialization complete\r\n");
    return 0;
}

void audio_loop(void)
{
    audio_ctx_t *ctx = &g_audio_ctx;
    int16_t *ptr;

    printf("[Audio] Starting audio loop...\r\n");
    ctx->running = true;

    while (ctx->running)
    {
        /* 等待输入缓冲有数据 */
        if (ctx->in_valid_num > 0)
        {
            /* 获取输入缓冲指针 */
            ptr = ctx->in_buf + ctx->in_read_idx * AUDIO_DMA_BUFFER_LEN;

            /* 解交织: 立体声 -> 左/右通道 */
            audio_deinterleave(ptr, ctx->block_left, ctx->block_right, AUDIO_BLOCK_LEN);

            /* 更新输入索引 */
            ctx->in_read_idx = (ctx->in_read_idx + 1) % AUDIO_DMA_BUFFER_COUNT;
            ctx->in_valid_num--;

            /* ===== 在此处插入 DSP/KWS 处理 ===== */
            /*
             * 原 demo 中的处理逻辑:
             *   - 将 block_left 送到 DSP 输入缓冲 (dsp_in_block)
             *   - 设置 input_ready_flag = 1
             *   - 等待 output_ready_flag
             *   - 读取 DSP 输出 (kws_cmd) 并显示结果
             */

            /* 简单直通: 将输入直接复制到输出 */
            ptr = ctx->out_buf + ctx->out_write_idx * AUDIO_DMA_BUFFER_LEN;
            audio_interleave(ctx->block_left, ctx->block_left, ptr, AUDIO_BLOCK_LEN);

            /* 更新输出索引 */
            ctx->out_write_idx = (ctx->out_write_idx + 1) % AUDIO_DMA_BUFFER_COUNT;
            ctx->out_valid_num++;
        }
    }
}

void audio_dma0_irq_handler(void)
{
    audio_ctx_t *ctx = &g_audio_ctx;
    S300_DMA_TypeDef *D = DMAC0;

    /* 清除 DMA 中断标志 */
    D->ClearTfr = 0xFFu;
    D->ClearBlock = 0xFFu;
    D->ClearSrcTran = 0xFFu;
    D->ClearDstTran = 0xFFu;
    D->ClearErr = 0xFFu;

    /* 检查录音通道 (通道 0) */
    if (is_dma_busy(EM_DMA0, 0) == 0)
    {
        ctx->in_valid_num++;
        ctx->in_write_idx = (ctx->in_write_idx + 1) % AUDIO_DMA_BUFFER_COUNT;

        /* 重新配置并启动下一次传输 */
        set_dma_std_address(EM_DMA0, 0,
                            (uint32_t)&I2S1->RXDMA,
                            (uint32_t)(ctx->in_buf + ctx->in_write_idx * AUDIO_DMA_BUFFER_LEN));
        set_dma_start(EM_DMA0, 0);
    }

    /* 检查播放通道 (通道 1) */
    if (is_dma_busy(EM_DMA0, 1) == 0)
    {
        ctx->out_valid_num--;
        ctx->out_read_idx = (ctx->out_read_idx + 1) % AUDIO_DMA_BUFFER_COUNT;

        /* 重新配置并启动下一次传输 */
        set_dma_std_address(EM_DMA0, 1,
                            (uint32_t)(ctx->out_buf + ctx->out_read_idx * AUDIO_DMA_BUFFER_LEN),
                            (uint32_t)&I2S1->TXDMA);
        set_dma_start(EM_DMA0, 1);
    }
}
