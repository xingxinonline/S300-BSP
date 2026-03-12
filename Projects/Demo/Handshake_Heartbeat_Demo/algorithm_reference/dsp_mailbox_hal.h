/**
 * @file dsp_mailbox_hal.h
 * @brief DSP 端 Mailbox 硬件抽象层
 * 
 * 本文件为 DSP 端提供 Mailbox 操作的硬件抽象接口。
 * DSP 开发者需要根据实际硬件配置修改此文件中的地址定义。
 * 
 * @note 将此文件连同 handshake_proto.h 一起复制到 DSP 工程中
 */
#ifndef DSP_MAILBOX_HAL_H
#define DSP_MAILBOX_HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * Mailbox 地址配置
 *
 * 当前握手 demo 的实测可用映射为“单 Mailbox 外设 + 双 FIFO”：
 *
 * CM4 视角:
 *   - MAILBOX_BASE     = 0x40019000
 *   - WRDATA (0x00): CM4 写入 → DSP 读取
 *   - RDDATA (0x08): DSP 写入 → CM4 读取
 *
 * DSP 视角:
 *   - DSP_MAILBOX_BASE = 0x44080400
 *   - WRDATA (0x00): DSP 写入 → CM4 读取
 *   - RDDATA (0x08): CM4 写入 → DSP 读取
 *
 * 不再使用 TX_BASE / RX_BASE 两个独立基址模型。
 *===========================================================================*/

/** DSP 视角的 Mailbox 基地址 */
#ifndef DSP_MAILBOX_BASE
#define DSP_MAILBOX_BASE        0x44080400u
#endif

/*===========================================================================
 * Mailbox 寄存器偏移
 *===========================================================================*/

#define MAILBOX_REG_WRDATA      0x00    /**< 写数据 FIFO */
#define MAILBOX_REG_RDDATA      0x08    /**< 读数据 FIFO */
#define MAILBOX_REG_STA         0x10    /**< 状态寄存器 */
#define MAILBOX_REG_CTRL        0x2C    /**< 控制寄存器 */

/*===========================================================================
 * 状态寄存器位定义
 *===========================================================================*/

#define MAILBOX_STA_EMPTY       (1u << 0)   /**< RX FIFO 空 */
#define MAILBOX_STA_FULL        (1u << 1)   /**< TX FIFO 满 */
#define MAILBOX_STA_HALF        (1u << 2)   /**< FIFO 半满 */

/*===========================================================================
 * 控制寄存器位定义
 *===========================================================================*/

#define MAILBOX_CTRL_CLR_TX     (1u << 0)   /**< 清空 TX FIFO */
#define MAILBOX_CTRL_CLR_RX     (1u << 1)   /**< 清空 RX FIFO */

/*===========================================================================
 * 内联函数：寄存器访问
 *===========================================================================*/

/** 读取 TX 状态寄存器 */
static inline uint32_t dsp_mbox_tx_sta(void)
{
    return *(volatile uint32_t *)(DSP_MAILBOX_BASE + MAILBOX_REG_STA);
}

/** 读取 RX 状态寄存器 */
static inline uint32_t dsp_mbox_rx_sta(void)
{
    return *(volatile uint32_t *)(DSP_MAILBOX_BASE + MAILBOX_REG_STA);
}

/*===========================================================================
 * API 函数
 *===========================================================================*/

/**
 * @brief 初始化 DSP Mailbox
 * 
 * 清空收发 FIFO，准备通信。
 */
static inline void dsp_mailbox_init(void)
{
    *(volatile uint32_t *)(DSP_MAILBOX_BASE + MAILBOX_REG_CTRL) =
        (MAILBOX_CTRL_CLR_TX | MAILBOX_CTRL_CLR_RX);
}

/**
 * @brief 检查是否有数据可读
 * @return 1 有数据, 0 无数据
 */
static inline int dsp_mailbox_has_data(void)
{
    return (dsp_mbox_rx_sta() & MAILBOX_STA_EMPTY) == 0;
}

/**
 * @brief 检查是否可以发送数据
 * @return 1 可以发送, 0 FIFO 满
 */
static inline int dsp_mailbox_can_send(void)
{
    return (dsp_mbox_tx_sta() & MAILBOX_STA_FULL) == 0;
}

/**
 * @brief 读取一个 32 位数据
 * @return 读取的数据
 * @note 调用前应先检查 dsp_mailbox_has_data()
 */
static inline uint32_t dsp_mailbox_read(void)
{
    return *(volatile uint32_t *)(DSP_MAILBOX_BASE + MAILBOX_REG_RDDATA);
}

/**
 * @brief 发送一个 32 位数据 (非阻塞)
 * @param data 要发送的数据
 * @return 0 成功, -1 FIFO 满
 */
static inline int dsp_mailbox_write(uint32_t data)
{
    if (!dsp_mailbox_can_send()) {
        return -1;
    }
    *(volatile uint32_t *)(DSP_MAILBOX_BASE + MAILBOX_REG_WRDATA) = data;
    return 0;
}

/**
 * @brief 发送一个 32 位数据 (阻塞等待)
 * @param data 要发送的数据
 * @param timeout 超时循环次数 (0 = 无限等待)
 * @return 0 成功, -1 超时
 */
static inline int dsp_mailbox_write_blocking(uint32_t data, uint32_t timeout)
{
    uint32_t loops = 0;
    
    while (!dsp_mailbox_can_send()) {
        if (timeout > 0 && ++loops >= timeout) {
            return -1;
        }
    }
    
    *(volatile uint32_t *)(DSP_MAILBOX_BASE + MAILBOX_REG_WRDATA) = data;
    return 0;
}

/**
 * @brief 读取一个 32 位数据 (阻塞等待)
 * @param out 输出数据指针
 * @param timeout 超时循环次数 (0 = 无限等待)
 * @return 0 成功, -1 超时
 */
static inline int dsp_mailbox_read_blocking(uint32_t *out, uint32_t timeout)
{
    uint32_t loops = 0;
    
    while (!dsp_mailbox_has_data()) {
        if (timeout > 0 && ++loops >= timeout) {
            return -1;
        }
    }
    
    *out = dsp_mailbox_read();
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* DSP_MAILBOX_HAL_H */
