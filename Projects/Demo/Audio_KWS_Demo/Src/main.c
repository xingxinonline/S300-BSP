/**
 * @file    main.c
 * @brief   Audio KWS Demo 主程序
 * @note    移植自 gitlab/feat/kws:test_kcx_audio 项目
 *          适配到 S300-BSP SDK 架构
 *
 * 功能说明:
 *   - 使用 I2S1 + WM8978 Codec 进行音频采集与播放
 *   - 基于 DMA 双缓冲实现低延迟音频流处理
 *   - 通过共享内存与 DSP 通信进行 KWS (关键词识别)
 *
 * DSP 通信协议 (共享内存):
 *   - 0x44040000: dsp_in_block  - 音频输入数据 (CM4 -> DSP)
 *   - 0x44040200: dsp_out_block - 音频输出数据 (DSP -> CM4)
 *   - 0x44040400: kws_cmd       - KWS 识别结果
 *   - 0x44040430: dsp_ready_flag    - DSP 就绪标志
 *   - 0x44040434: input_ready_flag  - 输入数据就绪标志
 *   - 0x44040438: output_ready_flag - 输出数据就绪标志
 *
 * DSP 固件加载:
 *   - DSP 固件位于 Algorithm_Models/Keyword_Spotting/
 *   - 需要通过 GDB 脚本或 Bootloader 预先加载到 DSP 内存
 *
 * 硬件连接 (Generic EVB):
 *   - PA3: I2S1_MCLK (FUNCTION_1)
 *   - PA6: I2S1_BCLK (FUNCTION_3)
 *   - PA7: I2S1_LRCLK (FUNCTION_3)
 *   - PA8: I2S1_DO (输出到 Codec, FUNCTION_3)
 *   - PA9: I2S1_DI (来自 Codec, FUNCTION_3)
 *   - PA0: WM8978 I2C_SCL (软件模拟 I2C, idx=1)
 *   - PA1: WM8978 I2C_SDA (软件模拟 I2C, idx=1)
 */

#include "s300.h"
#include "board.h"
#include "audio_app.h"
#include "rcc.h"
#include "rcc_s300.h"  /* For S300_RCC->CM4_SYS_SOFT_RSTN */
#include <stdio.h>
#include <string.h>

/*===========================================================================
 * DSP 共享内存定义 (来自原始 gitlab/feat/kws 代码)
 *===========================================================================*/

/* DSP 共享内存地址 */
#define DSP_IN_BLOCK_ADDR       0x44040000
#define DSP_OUT_BLOCK_ADDR      0x44040200
#define KWS_CMD_ADDR            0x44040400
#define DSP_READY_FLAG_ADDR     0x44040430
#define INPUT_READY_FLAG_ADDR   0x44040434
#define OUTPUT_READY_FLAG_ADDR  0x44040438
#define DSP_CALC_CYCLES_ADDR    0x4404043C

/* DSP 共享内存指针 */
static volatile int16_t  *dsp_in_block     = (volatile int16_t  *)DSP_IN_BLOCK_ADDR;
static volatile int16_t  *dsp_out_block    = (volatile int16_t  *)DSP_OUT_BLOCK_ADDR;
static volatile uint8_t  *kws_cmd          = (volatile uint8_t  *)KWS_CMD_ADDR;
static volatile uint32_t *dsp_ready_flag   = (volatile uint32_t *)DSP_READY_FLAG_ADDR;
static volatile uint32_t *input_ready_flag = (volatile uint32_t *)INPUT_READY_FLAG_ADDR;
static volatile uint32_t *output_ready_flag= (volatile uint32_t *)OUTPUT_READY_FLAG_ADDR;

/* 音频块长度 (采样点数，左右声道各 AUDIO_BLOCK_LEN 个采样) */
#define AUDIO_BLOCK_LEN         (AUDIO_DMA_BUFFER_LEN / 2)

/* KWS 中间缓冲区 (用于解交织) */
static int16_t audio_block_left[AUDIO_BLOCK_LEN];
static int16_t audio_block_right[AUDIO_BLOCK_LEN];

/* KWS 关键词列表 */
static const char *keywords[] = {
    "unknown",           /* 0 */
    "pai-zhang-zhaopian",/* 1: 拍张照片 */
    "kaishi-luxiang",    /* 2: 开始录像 */
    "tingzhi-luxiang",   /* 3: 停止录像 */
    "qidong-gensui",     /* 4: 启动跟随 */
    "jieshu-gensui",     /* 5: 结束跟随 */
    "dakai-buguangdeng", /* 6: 打开补光灯 */
    "guanbi-buguangdeng",/* 7: 关闭补光灯 */
};
#define KEYWORDS_COUNT  (sizeof(keywords) / sizeof(keywords[0]))

/* KWS 连续检测计数器 */
static uint16_t consecutive_count = 0;
static uint16_t target_idx_val = 0;
static uint8_t  counting_started = 0;
static uint16_t last_output_idx = 0;      /* 上次输出的关键词，防止重复 */
static uint32_t cooldown_frames = 0;      /* 冷却帧数，防止连续触发 */
#define KWS_COOLDOWN_FRAMES  50           /* 识别后冷却帧数 (~800ms @ 16ms/frame) */

/*===========================================================================
 * 辅助函数
 *===========================================================================*/

/**
 * @brief 立体声解交织: 交织格式 -> 左右声道分离
 * @param src       输入缓冲区 (交织格式: L0, R0, L1, R1, ...)
 * @param dst_left  左声道输出
 * @param dst_right 右声道输出
 * @param frame_num 采样帧数
 */
static void deinterleave(const int16_t *src, int16_t *dst_left, int16_t *dst_right, int16_t frame_num)
{
    for (int i = 0; i < frame_num; i++)
    {
        *dst_right++ = *src++;
        *dst_left++  = *src++;
    }
}

/**
 * @brief 立体声交织: 左右声道 -> 交织格式
 * @param src_left  左声道输入
 * @param src_right 右声道输入
 * @param dst       输出缓冲区 (交织格式)
 * @param frame_num 采样帧数
 */
static void interleave(const int16_t *src_left, const int16_t *src_right, int16_t *dst, int16_t frame_num)
{
    for (int i = 0; i < frame_num; i++)
    {
        *dst++ = *src_right++;
        *dst++ = *src_left++;
    }
}

/**
 * @brief 显示 KWS 识别结果
 * @param output KWS 输出概率数组
 * @return 识别到的关键词索引
 */
static uint16_t show_kws_result(volatile uint8_t *output)
{
    uint8_t  max_val = 0;
    uint16_t idx_val = 0;
    uint8_t  temp;

    /* 冷却期间不处理 */
    if (cooldown_frames > 0)
    {
        cooldown_frames--;
        return 0;
    }

    /* 找到概率最大的关键词 */
    for (uint16_t i = 0; i < KEYWORDS_COUNT; i++)
    {
        temp = output[i];
        if (max_val < temp)
        {
            max_val = temp;
            idx_val = i;
        }
    }

    /* 连续检测逻辑: 需要连续多帧识别到同一关键词才输出 */
    if (!counting_started || idx_val == target_idx_val)
    {
        if (!counting_started)
        {
            target_idx_val = idx_val;
            counting_started = 1;
        }
        
        if (max_val >= 90)
            consecutive_count++;

        /* 连续识别 8 次且概率 >= 90 时输出结果 */
        if (consecutive_count >= 8 && target_idx_val != 0)
        {
            /* 输出识别结果 */
            if (idx_val < KEYWORDS_COUNT)
            {
                printf("[KWS] >>> %s (confidence=%u)\r\n", keywords[idx_val], max_val);
            }
            else
            {
                printf("[KWS] >>> Unknown (confidence=%u)\r\n", max_val);
            }

            /* 重置状态并进入冷却期 */
            consecutive_count = 0;
            counting_started = 0;
            last_output_idx = idx_val;
            cooldown_frames = KWS_COOLDOWN_FRAMES;
        }
    }
    else
    {
        /* 识别到不同的关键词，重置计数 */
        consecutive_count = 1;
        target_idx_val = idx_val;
    }

    return idx_val;
}

/*===========================================================================
 * 中断处理
 *===========================================================================*/

/**
 * @brief DMA0 中断服务函数
 */
void DMA0_IRQHandler(void)
{
    audio_dma0_irq_handler();
}

/**
 * @brief I2S 音频接收与 KWS 处理循环
 */
static void i2s_kws_loop(void)
{
    audio_ctx_t *ctx = audio_get_ctx();
    int16_t *ptr;

    /* 初始化 DSP 同步标志 */
    *dsp_ready_flag = 0;
    *input_ready_flag = 0;
    *output_ready_flag = 0;

    printf("[KWS] Waiting for DSP ready...\r\n");

    /* 等待 DSP 就绪 */
    while (*dsp_ready_flag == 0)
    {
        /* 可以添加超时检测 */
    }

    printf("[KWS] DSP ready! Starting keyword detection...\r\n");

    /* 主处理循环 */
    while (1)
    {
        /* 检查是否有新的音频数据 */
        if (ctx->in_valid_num > 0)
        {
            /* 获取输入缓冲区指针 */
            ptr = ctx->in_buf + ctx->in_read_idx * AUDIO_DMA_BUFFER_LEN;

            /* 解交织音频数据
             * 原始代码: deinterleave(ptr, dsp_in_block, audio_block_rigth, AUDIO_BLOCK_LEN)
             * 将左声道(第一个输出参数)写入 dsp_in_block
             */
            deinterleave(ptr, (int16_t *)dsp_in_block, audio_block_right, AUDIO_BLOCK_LEN);

            /* 更新输入缓冲区索引 */
            ctx->in_read_idx = (ctx->in_read_idx + 1) % AUDIO_DMA_BUFFER_COUNT;
            ctx->in_valid_num--;

            /* 获取输出缓冲区指针 */
            ptr = ctx->out_buf + ctx->out_write_idx * AUDIO_DMA_BUFFER_LEN;

            /* 将输入数据直接回放 (passthrough) */
            interleave((const int16_t *)dsp_in_block, (const int16_t *)dsp_in_block, ptr, AUDIO_BLOCK_LEN);

            /* 更新输出缓冲区索引 */
            ctx->out_write_idx = (ctx->out_write_idx + 1) % AUDIO_DMA_BUFFER_COUNT;
            ctx->out_valid_num++;

            /* 通知 DSP: 输入数据已就绪 */
            *input_ready_flag = 1;

            /* 等待 DSP 处理完成 */
            while (*output_ready_flag == 0)
            {
                /* 可以添加超时检测 */
            }
            *output_ready_flag = 0;

            /* 显示 KWS 识别结果 */
            show_kws_result(kws_cmd);
        }
    }
}

/**
 * @brief DSP 初始化完成标记函数
 * @note  GDB 脚本在此函数设置断点，用于加载 DSP 固件
 */
__attribute__((noinline)) void dsp_firmware_load_point(void)
{
    /* 此函数仅作为 GDB 断点标记，DSP 固件通过 GDB restore 命令加载 */
    __asm volatile("nop");
}

/**
 * @brief 主函数
 */
int main(void)
{
    int ret;

    /* 板级初始化: 时钟 + 调试串口 */
    board_init();

    printf("\r\n");
    printf("========================================\r\n");
    printf("  S300 Audio KWS Demo\r\n");
    printf("  (Ported from gitlab/feat/kws)\r\n");
    printf("========================================\r\n");
    printf("\r\n");

    /* =========================================
     * 参考原始 demo 初始化顺序:
     * 1. GPIO I2S 引脚配置 (在 audio_init 中)
     * 2. WM8978 Codec 初始化 (在 audio_init 中)
     * 3. Audio PLL + I2S 配置 (在 audio_init 中)
     * 4. DSP PLL + DSP 复位
     * 5. DMA 中断使能 + I2S DMA 模式 (在 audio_init 中)
     * ========================================= */

    /* 初始化音频子系统 (GPIO, WM8978, Audio PLL, I2S) */
    ret = audio_init();
    if (ret != 0)
    {
        printf("[Main] Audio init failed: %d\r\n", ret);
        printf("[Main] Please check WM8978 connection!\r\n");
        while (1)
        {
            __WFI();
        }
    }

    /* 初始化 DSP PLL (参考原始代码: init_dsp_pll(6,768,500000, 2, 2)) */
    printf("[Main] Initializing DSP PLL...\r\n");
    rcc_init_dsp_pll(6, 768, 500000, 2, 2);

    /* DSP 热复位 (参考原始代码: set_dsp_warm_reset + CM4_SYS_SOFT_RSTN_REG |= 1) */
    printf("[Main] Resetting DSP...\r\n");
    rcc_set_dsp_warm_reset(true);
    RCC->CM4_SYS_SOFT_RSTN |= 1;

    /* 
     * GDB 断点位置: DSP 固件在此加载
     * GDB 脚本会在 dsp_firmware_load_point 设置断点，
     * 程序暂停后通过 restore 命令加载 DSP 固件到 DTCM/PTCM
     */
    dsp_firmware_load_point();

    /* 等待 DSP 启动 */
    for (volatile int i = 0; i < 100000; i++);
    
    printf("\r\n");
    printf("[Main] DSP Communication Protocol:\r\n");
    printf("  - dsp_in_block:     0x%08X\r\n", DSP_IN_BLOCK_ADDR);
    printf("  - dsp_out_block:    0x%08X\r\n", DSP_OUT_BLOCK_ADDR);
    printf("  - kws_cmd:          0x%08X\r\n", KWS_CMD_ADDR);
    printf("  - dsp_ready_flag:   0x%08X\r\n", DSP_READY_FLAG_ADDR);
    printf("  - input_ready_flag: 0x%08X\r\n", INPUT_READY_FLAG_ADDR);
    printf("  - output_ready_flag:0x%08X\r\n", OUTPUT_READY_FLAG_ADDR);
    printf("\r\n");
    printf("[Main] DSP firmware loaded via GDB\r\n");
    printf("[Main] Keywords supported:\r\n");
    for (size_t i = 0; i < KEYWORDS_COUNT; i++)
    {
        printf("  [%zu] %s\r\n", i, keywords[i]);
    }
    printf("\r\n");

    /* 进入 I2S + KWS 处理循环 */
    i2s_kws_loop();

    /* 不会到达这里 */
    return 0;
}
