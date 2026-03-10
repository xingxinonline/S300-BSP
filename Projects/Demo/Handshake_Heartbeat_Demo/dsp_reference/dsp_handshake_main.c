/**
 * @file dsp_handshake_main.c
 * @brief DSP 端握手 + Heartbeat 演示程序（参考实现）
 * 
 * 本文件提供 DSP 端的握手和 Heartbeat 处理参考代码。
 * 实际使用时需要集成到 CEVA DSP 工程中。
 * 
 * 启动流程（由 CM4 控制）：
 *   1. CM4 启动时让 DSP 保持域复位 (DSP_CEVA_RST_CTRL=0)
 *   2. CM4 完成初始化并配置 DSP PLL
 *   3. CM4 释放 DSP 域复位 (DSP_CEVA_RST_CTRL=1)
 *   4. CM4 触发 DSP warm reset，DSP 开始执行本程序
 *   5. DSP 初始化 Mailbox，等待握手
 * 
 * 功能说明：
 *   1. DSP 启动后初始化 Mailbox
 *   2. 等待 CM4 的握手消息 (HANDSHAKE_INIT)
 *   3. 收到后回复 HANDSHAKE_ACK，完成握手
 *   4. 进入主循环，等待 CM4 的 HEARTBEAT_TRIGGER 消息
 *   5. 收到触发消息后打印 Heartbeat 并回复
 * 
 * 相关文件：
 *   - handshake_proto.h    - 协议定义（需复制到 DSP 工程）
 *   - dsp_mailbox_hal.h    - Mailbox 硬件抽象层
 * 
 * @see docs/CM4_DSP_Handshake_Design.md 完整设计文档
 */

#include <stdio.h>
#include <stdint.h>

/* 协议定义（需复制到 DSP 工程） */
#include "handshake_proto.h"

/* Mailbox 硬件抽象层 */
#include "dsp_mailbox_hal.h"

/*===========================================================================
 * 握手处理
 *===========================================================================*/

/** DSP Heartbeat 计数器 */
static uint32_t g_dsp_heartbeat_count = 0;

static void dsp_send_handshake_ack(void)
{
    if (dsp_mailbox_write(HANDSHAKE_MSG_ACK) == 0) {
        printf("[DSP] Sent HANDSHAKE_ACK (0x%08lX)\r\n",
               (unsigned long)HANDSHAKE_MSG_ACK);
    }
}

/**
 * @brief DSP 端等待握手
 * 
 * 阻塞等待 CM4 发送 HANDSHAKE_INIT，然后回复 HANDSHAKE_ACK。
 * 
 * @return 0 成功, <0 超时
 */
static int dsp_wait_handshake(void)
{
    printf("[DSP] Waiting for CM4 handshake...\r\n");
    
    /* 
     * 实际实现中可以添加超时机制
     * 这里为简化示例，使用无限等待
     */
    while (1) {
        if (dsp_mailbox_has_data()) {
            uint32_t msg = dsp_mailbox_read();
            printf("[DSP] Received: 0x%08lX\r\n", (unsigned long)msg);
            
            if (msg == HANDSHAKE_MSG_INIT) {
                /* 收到握手初始化消息，回复 ACK */
                dsp_send_handshake_ack();
                printf("[DSP] Handshake completed!\r\n");
                return 0;
            }
        }
        
        /* 简单延时，避免忙等 */
        for (volatile int i = 0; i < 10000; i++) {
            /* spin */
        }
    }
    
    return -1;  /* 理论上不会到达这里 */
}

/*===========================================================================
 * Heartbeat 处理
 *===========================================================================*/

/**
 * @brief 处理 CM4 发送的 Heartbeat 触发消息
 * @param msg 接收到的消息
 */
static void dsp_handle_heartbeat_trigger(uint32_t msg)
{
    uint16_t seq = HEARTBEAT_GET_SEQ(msg);
    
    /* DSP 打印 Heartbeat */
    printf("[DSP] Heartbeat #%lu triggered by CM4 (seq=%u)\r\n",
           (unsigned long)g_dsp_heartbeat_count,
           seq);
    g_dsp_heartbeat_count++;
    
    /* 回复 Heartbeat 确认 */
    uint32_t reply = HEARTBEAT_MAKE_REPLY((uint8_t)seq, DSP_STATUS_OK);
    if (dsp_mailbox_write(reply) == 0) {
        printf("[DSP] Sent HEARTBEAT_REPLY\r\n");
    }
}

/**
 * @brief DSP 主循环处理函数
 * 
 * 持续检查 Mailbox 消息并处理
 */
static void dsp_main_loop(void)
{
    printf("[DSP] Entering main loop, waiting for heartbeat triggers...\r\n");
    
    while (1) {
        /* 检查是否有消息 */
        if (dsp_mailbox_has_data()) {
            uint32_t msg = dsp_mailbox_read();
            
            if (msg == HANDSHAKE_MSG_INIT) {
                /* 幂等握手：若 CM4 因重启/清 FIFO 未收到 ACK，则重发 ACK */
                dsp_send_handshake_ack();
            } else if (HEARTBEAT_IS_TRIGGER(msg)) {
                dsp_handle_heartbeat_trigger(msg);
            } else {
                printf("[DSP] Unknown message: 0x%08lX\r\n", (unsigned long)msg);
            }
        }
        
        /* 在实际 DSP 工程中，这里可能需要调用其他处理函数 */
        /* 例如：算法推理、数据处理等 */
        
        /* 简单延时 */
        for (volatile int i = 0; i < 1000; i++) {
            /* spin */
        }
    }
}

/*===========================================================================
 * 主函数
 *===========================================================================*/

/**
 * @brief DSP 端主入口
 * 
 * 在实际 DSP 工程中，可能需要将此函数集成到现有的初始化流程中。
 */
int main(void)
{
    printf("\r\n");
    printf("============================================\r\n");
    printf("  S300 DSP Handshake & Heartbeat Demo\r\n");
    printf("============================================\r\n");
    
    /* 初始化 Mailbox */
    dsp_mailbox_init();
    
    /* 等待与 CM4 握手完成 */
    if (dsp_wait_handshake() != 0) {
        printf("[DSP] Handshake failed!\r\n");
        while (1) {
            /* error loop */
        }
    }
    
    printf("\r\n[DSP] === Handshake done, ready for heartbeat ===\r\n\r\n");
    
    /* 进入主循环 */
    dsp_main_loop();
    
    return 0;
}
