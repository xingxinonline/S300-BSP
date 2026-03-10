/**
 * @file handshake_proto.h
 * @brief CM4 与 DSP 握手协议定义
 * 
 * 本文件定义了 CM4 和 DSP 之间握手流程所使用的消息格式。
 * 该文件应同时被 CM4 侧和 DSP 侧代码引用，确保双方数据结构一致。
 * 
 * DSP 启动流程（CM4 侧）：
 *   1. CM4 启动时让 DSP 保持域复位 (DSP_CEVA_RST_CTRL=0, 0x4000a018)
 *   2. CM4 完成自身初始化（时钟、UART、Mailbox 等）
 *   3. 在 DSP 域复位状态下配置 DSP PLL
 *   4. 释放 DSP 域复位 (DSP_CEVA_RST_CTRL=1)
 *   5. 触发 DSP warm reset (自弹起，仅复位内核)
 *   6. DSP 开始运行，初始化 Mailbox，等待握手
 * 
 * 握手流程：
 *   1. CM4 周期性发送 HANDSHAKE_INIT 消息
 *   2. DSP 收到 INIT 消息后回复 HANDSHAKE_ACK
 *   3. CM4 收到 ACK，握手完成
 *   4. 双方进入工作状态，周期性发送 HEARTBEAT
 * 
 * Heartbeat 触发方式：
 *   - CM4: 由 SysTick 定时器触发打印
 *   - DSP: 由 CM4 mailbox 消息触发打印
 * 
 * 版本: 1.0
 * 
 * @see mailbox_proto.h 通用邮箱消息格式
 */
#ifndef HANDSHAKE_PROTO_H
#define HANDSHAKE_PROTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 协议版本
 *===========================================================================*/

/** 协议版本号 */
#define HANDSHAKE_PROTOCOL_VERSION    0x0100u  /* v1.0 */

/*===========================================================================
 * 握手消息定义
 *===========================================================================*/

/**
 * @brief 握手初始化消息 (CM4 → DSP)
 * 
 * CM4 发送此消息给 DSP，表示 CM4 已准备好，等待 DSP 回应。
 */
#define HANDSHAKE_MSG_INIT            0x5A5A5A5Au

/**
 * @brief 握手确认消息 (DSP → CM4)
 * 
 * DSP 收到 INIT 消息后回复此消息，表示握手完成。
 */
#define HANDSHAKE_MSG_ACK             0xA5A5A5A5u

/*===========================================================================
 * Heartbeat 消息定义
 *===========================================================================*/

/**
 * @brief Heartbeat 触发消息 (CM4 → DSP)
 * 
 * CM4 周期性发送此消息给 DSP，触发 DSP 打印 heartbeat。
 * 消息格式: HEARTBEAT_MSG_TRIGGER | sequence_number
 * 
 * Bit [31:16]: 消息类型标识 (0xBEA7)
 * Bit [15:0]:  序列号 (0 ~ 65535 循环)
 */
#define HEARTBEAT_MSG_TRIGGER_MASK    0xBEA70000u
#define HEARTBEAT_MSG_SEQ_MASK        0x0000FFFFu

/** @brief 构造 Heartbeat 触发消息 */
#define HEARTBEAT_MAKE_TRIGGER(seq) \
    (HEARTBEAT_MSG_TRIGGER_MASK | ((uint32_t)(seq) & HEARTBEAT_MSG_SEQ_MASK))

/** @brief 检查是否为 Heartbeat 触发消息 */
#define HEARTBEAT_IS_TRIGGER(msg) \
    (((msg) & 0xFFFF0000u) == HEARTBEAT_MSG_TRIGGER_MASK)

/** @brief 从 Heartbeat 消息中提取序列号 */
#define HEARTBEAT_GET_SEQ(msg) \
    ((uint16_t)((msg) & HEARTBEAT_MSG_SEQ_MASK))

/**
 * @brief Heartbeat 回复消息 (DSP → CM4, 可选)
 * 
 * DSP 收到 Heartbeat 触发后可以回复此消息。
 * 消息格式: HEARTBEAT_MSG_REPLY | sequence_number | status
 * 
 * Bit [31:16]: 消息类型标识 (0xACE0)
 * Bit [15:8]:  序列号 (回显 CM4 发送的序列号的低 8 位)
 * Bit [7:0]:   DSP 状态 (0 = OK, 其他 = 错误码)
 */
#define HEARTBEAT_MSG_REPLY_MASK      0xACE00000u
#define HEARTBEAT_REPLY_SEQ_SHIFT     8
#define HEARTBEAT_REPLY_STATUS_MASK   0x000000FFu

/** @brief 构造 Heartbeat 回复消息 */
#define HEARTBEAT_MAKE_REPLY(seq, status) \
    (HEARTBEAT_MSG_REPLY_MASK | \
     (((uint32_t)((seq) & 0xFF)) << HEARTBEAT_REPLY_SEQ_SHIFT) | \
     ((uint32_t)(status) & HEARTBEAT_REPLY_STATUS_MASK))

/** @brief 检查是否为 Heartbeat 回复消息 */
#define HEARTBEAT_IS_REPLY(msg) \
    (((msg) & 0xFFF00000u) == HEARTBEAT_MSG_REPLY_MASK)

/** @brief 从 Heartbeat 回复中提取序列号 */
#define HEARTBEAT_REPLY_GET_SEQ(msg) \
    ((uint8_t)(((msg) >> HEARTBEAT_REPLY_SEQ_SHIFT) & 0xFF))

/** @brief 从 Heartbeat 回复中提取状态 */
#define HEARTBEAT_REPLY_GET_STATUS(msg) \
    ((uint8_t)((msg) & HEARTBEAT_REPLY_STATUS_MASK))

/*===========================================================================
 * 超时配置
 *===========================================================================*/

/** 握手超时时间 (ms) */
#ifndef HANDSHAKE_TIMEOUT_MS
#define HANDSHAKE_TIMEOUT_MS          1000u
#endif

/** 握手重试间隔 (ms) */
#ifndef HANDSHAKE_RETRY_INTERVAL_MS
#define HANDSHAKE_RETRY_INTERVAL_MS   100u
#endif

/** Heartbeat 发送间隔 (ms) */
#ifndef HEARTBEAT_INTERVAL_MS
#define HEARTBEAT_INTERVAL_MS         1000u
#endif

/*===========================================================================
 * 状态定义
 *===========================================================================*/

/** 握手状态 */
typedef enum {
    HANDSHAKE_STATE_IDLE = 0,       /**< 空闲状态 */
    HANDSHAKE_STATE_WAITING,        /**< 等待握手 */
    HANDSHAKE_STATE_DONE,           /**< 握手完成 */
    HANDSHAKE_STATE_TIMEOUT,        /**< 握手超时 */
} HandshakeState_t;

/** DSP 运行状态 */
typedef enum {
    DSP_STATUS_OK = 0,              /**< 正常 */
    DSP_STATUS_BUSY = 1,            /**< 忙碌 */
    DSP_STATUS_ERROR = 2,           /**< 错误 */
} DspStatus_t;

#ifdef __cplusplus
}
#endif

#endif /* HANDSHAKE_PROTO_H */
