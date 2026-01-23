/**
 * @file    audio_app.h
 * @brief   音频应用层接口 - I2S DMA 双缓冲 + WM8978 Codec
 * @note    参考 gitlab/feat/kws:test_kcx_audio/cortex-m4-kcx/app/main.c
 */

#ifndef AUDIO_APP_H
#define AUDIO_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/*===========================================================================
 * 音频配置参数 (与原 demo 对齐)
 *===========================================================================*/

/** @brief DMA 单次传输 buffer 长度 (立体声采样点数 * 2 通道 * 2 字节) */
#ifndef AUDIO_DMA_BUFFER_LEN
#define AUDIO_DMA_BUFFER_LEN        (512)
#endif

/** @brief 循环 buffer 数量 (双缓冲或多缓冲) */
#ifndef AUDIO_DMA_BUFFER_COUNT
#define AUDIO_DMA_BUFFER_COUNT      (8)
#endif

/** @brief 单通道块长度 (帧数) */
#define AUDIO_BLOCK_LEN             (AUDIO_DMA_BUFFER_LEN / 2)

/*===========================================================================
 * 数据结构
 *===========================================================================*/

/** @brief 音频上下文结构体 */
typedef struct
{
    /* DMA 环形缓冲索引与计数 */
    volatile uint8_t in_read_idx;
    volatile uint8_t in_write_idx;
    volatile int32_t in_valid_num;

    volatile uint8_t out_read_idx;
    volatile uint8_t out_write_idx;
    volatile int32_t out_valid_num;

    /* DMA 缓冲区 (静态分配，按 4 字节对齐) */
    int16_t in_buf[AUDIO_DMA_BUFFER_COUNT * AUDIO_DMA_BUFFER_LEN];
    int16_t out_buf[AUDIO_DMA_BUFFER_COUNT * AUDIO_DMA_BUFFER_LEN];

    /* 解交织后的单通道数据块 */
    int16_t block_left[AUDIO_BLOCK_LEN];
    int16_t block_right[AUDIO_BLOCK_LEN];

    /* 运行标志 */
    volatile bool running;
} audio_ctx_t;

/*===========================================================================
 * API 函数
 *===========================================================================*/

/**
 * @brief   初始化音频子系统
 * @note    配置 Audio PLL, I2S, WM8978 Codec 及 DMA
 * @return  0=成功, 负值=错误
 */
int audio_init(void);

/**
 * @brief   启动音频录放循环
 * @note    阻塞式循环，内部使用 DMA 中断驱动缓冲
 */
void audio_loop(void);

/**
 * @brief   DMA0 中断处理回调 (需在 IRQ 入口调用)
 */
void audio_dma0_irq_handler(void);

/**
 * @brief   获取音频上下文指针 (供调试或外部访问)
 */
audio_ctx_t *audio_get_ctx(void);

/*===========================================================================
 * 工具函数
 *===========================================================================*/

/**
 * @brief   立体声交织: 两个单通道 -> 一个交织流 (L/R/L/R...)
 */
void audio_interleave(const int16_t *src_l, const int16_t *src_r, int16_t *dst, uint16_t frame_num);

/**
 * @brief   立体声解交织: 一个交织流 -> 两个单通道
 */
void audio_deinterleave(const int16_t *src, int16_t *dst_l, int16_t *dst_r, uint16_t frame_num);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_APP_H */
