/**
 * @file    main.c
 * @brief   Audio KWS Demo 主程序
 * @note    移植自 gitlab/feat/kws:test_kcx_audio 项目
 *          适配到 S300-BSP SDK 架构
 *
 * 功能说明:
 *   - 使用 I2S1 + Codec 进行音频采集与播放
 *   - 基于 DMA 双缓冲实现低延迟音频流处理
 *   - 通过 Mailbox 与 DSP 通信进行 KWS (关键词识别)
 *
 * DSP 通信协议 (Mailbox):
 *   - M4 → DSP: 音频数据就绪通知 (payload = 音频块偏移)
 *   - DSP → M4: KWS 识别结果 (MSG_TYPE_KWS | 结果偏移)
 *   - 使用统一的偏移地址方式，参考检测 Demo
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
 *   - PA0: Codec I2C_SCL (软件模拟 I2C)
 *   - PA1: Codec I2C_SDA (软件模拟 I2C)
 */

#include "s300.h"
#include "board.h"
#include "audio_app.h"
#include "rcc.h"
#include "rcc_s300.h"
#include "mailbox.h"
#include "mailbox_proto.h"
#include "kws_proto.h"
#include <stdio.h>
#include <string.h>

/*===========================================================================
 * DSP 音频数据地址定义
 * 音频输入数据仍需写入固定地址供 DSP 读取
 *===========================================================================*/

/** 音频输入块地址 (M4 → DSP) */
#define DSP_AUDIO_IN_ADDR       0x44040000u

/** 音频块长度 (采样点数，左右声道各 AUDIO_BLOCK_LEN 个采样) */
#define AUDIO_BLOCK_LEN         (AUDIO_DMA_BUFFER_LEN / 2)

/*===========================================================================
 * DSP 轮询模式兼容 (临时，待 DSP 升级后移除)
 * DSP 当前仍使用 input_ready_flag 轮询模式
 *===========================================================================*/

/** DSP 输入就绪标志地址 (DSP 轮询此地址) */
#define DSP_INPUT_READY_FLAG_ADDR   0x44040434u

/** DSP 输出就绪标志地址 (DSP 设置此地址) */
#define DSP_OUTPUT_READY_FLAG_ADDR  0x44040438u

/** DSP KWS 结果地址 (DSP 轮询模式下的结果) */
#define DSP_KWS_CMD_ADDR            0x44040400u

/* DSP 标志指针 */
static volatile uint32_t *dsp_input_ready_flag  = (volatile uint32_t *)DSP_INPUT_READY_FLAG_ADDR;
static volatile uint32_t *dsp_output_ready_flag = (volatile uint32_t *)DSP_OUTPUT_READY_FLAG_ADDR;
static volatile uint8_t  *dsp_kws_cmd           = (volatile uint8_t *)DSP_KWS_CMD_ADDR;

/* 音频输入缓冲区指针 */
static volatile int16_t *dsp_audio_in = (volatile int16_t *)DSP_AUDIO_IN_ADDR;

/* KWS 中间缓冲区 (用于解交织) */
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

/*===========================================================================
 * Mailbox 通信
 *===========================================================================*/

/**
 * @brief 初始化 Mailbox 通信
 */
static void kws_mailbox_init(void)
{
    /* 初始化邮箱 (4 个槽位，无中断) */
    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    printf("[KWS] Mailbox initialized\r\n");
}

/**
 * @brief 通过 Mailbox 发送命令到 DSP
 * @param cmd 命令类型 (MAILBOX_CMD_START, MAILBOX_CMD_STOP, etc.)
 * @param param 命令参数
 */
static void kws_send_command(uint32_t cmd, uint32_t param)
{
    uint32_t msg = MAILBOX_MAKE_MSG(cmd, param);
    write_mailbox(MAILBOX_BASE, msg);
}

/**
 * @brief 通知 DSP 音频数据已就绪
 * @param offset 音频数据相对 DSP_AUDIO_IN_ADDR 的偏移
 */
static void kws_notify_audio_ready(uint32_t offset)
{
    /* 使用 MAILBOX_CMD_AUDIO_DATA 命令通知 DSP */
    uint32_t msg = MAILBOX_MAKE_MSG(MAILBOX_CMD_AUDIO_DATA, offset);
    write_mailbox(MAILBOX_BASE, msg);
}

/**
 * @brief 处理 Mailbox 消息
 */
static void kws_mailbox_poll(void)
{
    /* 检查是否有邮箱消息 */
    while (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0)
    {
        uint32_t msg = read_mailbox(MAILBOX_BASE);
        uint32_t msg_type = MAILBOX_GET_MSG_TYPE(msg);
        uint32_t payload = MAILBOX_GET_PAYLOAD(msg);

        switch (msg_type) {
        case MAILBOX_MSG_TYPE_KWS: {
            /* KWS 识别结果：基址 + 偏移 = 实际地址 */
            uintptr_t addr = (uintptr_t)DSP_KWS_BASE_ADDR + (uintptr_t)payload;
            const volatile KWSResult_t *result = (const volatile KWSResult_t*)addr;
            
            /* 校验 magic */
            if (!KWS_RESULT_IS_VALID(result)) {
                printf("[KWS] Invalid result @0x%08lX (magic=0x%08lX)\r\n", 
                       (unsigned long)addr, (unsigned long)result->magic);
                break;
            }
            
            /* 处理识别结果 */
            if (KWS_IS_VALID_KEYWORD(result->keyword_idx)) {
                if (result->keyword_idx < KEYWORDS_COUNT) {
                    printf("[KWS] >>> %s (confidence=%u, frame=%lu)\r\n", 
                           keywords[result->keyword_idx], 
                           result->confidence,
                           (unsigned long)result->frame_id);
                } else {
                    printf("[KWS] >>> keyword[%u] (confidence=%u)\r\n",
                           result->keyword_idx, result->confidence);
                }
            }
            break;
        }
        
        case MAILBOX_MSG_TYPE_NO_RESULT:
            /* 本帧无识别结果 */
            break;
            
        default:
            /* 忽略未知消息类型 */
            break;
        }
    }
}

/**
 * @brief I2S 音频接收与 KWS 处理循环
 */
static void i2s_kws_loop(void)
{
    audio_ctx_t *ctx = audio_get_ctx();
    int16_t *ptr;

    printf("[KWS] Mode: Mailbox\r\n");
    printf("[KWS] Audio input addr: 0x%08lX\r\n", (unsigned long)DSP_AUDIO_IN_ADDR);
    printf("[KWS] KWS result base:  0x%08lX\r\n", (unsigned long)DSP_KWS_BASE_ADDR);
    
    /* 初始化 Mailbox */
    kws_mailbox_init();
    
    /* 发送启动命令 */
    kws_send_command(MAILBOX_CMD_START, 0);
    printf("[KWS] Sent START command to DSP\r\n");
    printf("[KWS] Waiting for DSP response...\r\n");

    /* 主处理循环 */
    while (1)
    {
        /* 检查 Mailbox 消息 */
        kws_mailbox_poll();
        
        /* 检查是否有新的音频数据 */
        if (ctx->in_valid_num > 0)
        {
            /* 获取输入缓冲区指针 */
            ptr = ctx->in_buf + ctx->in_read_idx * AUDIO_DMA_BUFFER_LEN;

            /* 解交织音频数据，左声道写入 DSP 音频输入区 */
            deinterleave(ptr, (int16_t *)dsp_audio_in, audio_block_right, AUDIO_BLOCK_LEN);

            /* 更新输入缓冲区索引 */
            ctx->in_read_idx = (ctx->in_read_idx + 1) % AUDIO_DMA_BUFFER_COUNT;
            ctx->in_valid_num--;

            /* 获取输出缓冲区指针 */
            ptr = ctx->out_buf + ctx->out_write_idx * AUDIO_DMA_BUFFER_LEN;

            /* 将输入数据直接回放 (passthrough) */
            interleave((const int16_t *)dsp_audio_in, (const int16_t *)dsp_audio_in, ptr, AUDIO_BLOCK_LEN);

            /* 更新输出缓冲区索引 */
            ctx->out_write_idx = (ctx->out_write_idx + 1) % AUDIO_DMA_BUFFER_COUNT;
            ctx->out_valid_num++;

            /* 通知 DSP: 音频数据已就绪 */
            /* 方式1: Mailbox 通知 (未来 DSP 升级后使用) */
            kws_notify_audio_ready(0);
            
            /* 方式2: 轮询模式兼容 (当前 DSP 使用) */
            *dsp_input_ready_flag = 1;
        }
        
        /* 检查 DSP 轮询模式输出 (兼容旧 DSP 固件) */
        if (*dsp_output_ready_flag)
        {
            *dsp_output_ready_flag = 0;
            
            /* 查找最大置信度的关键词 */
            uint8_t max_val = 0;
            uint8_t max_idx = 0;
            for (int i = 0; i < 8; i++)
            {
                if (dsp_kws_cmd[i] > max_val)
                {
                    max_val = dsp_kws_cmd[i];
                    max_idx = i;
                }
            }
            
            /* 输出识别结果 */
            if (max_idx > 0 && max_val >= 90)
            {
                if (max_idx < KEYWORDS_COUNT)
                {
                    printf("[KWS] >>> %s (confidence=%u)\r\n", keywords[max_idx], max_val);
                }
                else
                {
                    printf("[KWS] >>> keyword[%u] (confidence=%u)\r\n", max_idx, max_val);
                }
            }
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
    printf("  S300 Audio KWS Demo (Mailbox Mode)\r\n");
    printf("========================================\r\n");
    printf("\r\n");

    /* =========================================
     * 初始化顺序:
     * 1. GPIO I2S 引脚配置 (在 audio_init 中)
     * 2. Codec 初始化 (在 audio_init 中)
     * 3. Audio PLL + I2S 配置 (在 audio_init 中)
     * 4. DSP PLL + DSP 复位
     * 5. DMA 中断使能 + I2S DMA 模式 (在 audio_init 中)
     * ========================================= */

    /* 初始化音频子系统 */
    ret = audio_init();
    if (ret != 0)
    {
        printf("[Main] Audio init failed: %d\r\n", ret);
        printf("[Main] Please check codec connection!\r\n");
        while (1)
        {
            __WFI();
        }
    }

    /* 初始化 DSP PLL */
    printf("[Main] Initializing DSP PLL...\r\n");
    rcc_init_dsp_pll(6, 768, 500000, 2, 2);

    /* DSP 热复位 */
    printf("[Main] Resetting DSP...\r\n");
    rcc_set_dsp_warm_reset(true);
    RCC->CM4_SYS_SOFT_RSTN |= 1;

    /* 
     * GDB 断点位置: DSP 固件在此加载
     * GDB 脚本会在 dsp_firmware_load_point 设置断点，
     * 程序暂停后通过 restore 命令加载 DSP 固件到 DTCM/PTCM
     */
    dsp_firmware_load_point();

    /* 释放 DSP 复位，启动 DSP 运行 */
    printf("[Main] Starting DSP...\r\n");
    rcc_set_dsp_warm_reset(false);

    /* 等待 DSP 启动 */
    for (volatile int i = 0; i < 100000; i++);
    
    printf("\r\n");
    printf("[Main] DSP Communication: Mailbox\r\n");
    printf("[Main] Audio input:  0x%08lX\r\n", (unsigned long)DSP_AUDIO_IN_ADDR);
    printf("[Main] KWS base:     0x%08lX\r\n", (unsigned long)DSP_KWS_BASE_ADDR);
    printf("\r\n");
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
