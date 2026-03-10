/**
 * @file main.c
 * @brief CM4 与 DSP 握手 + Heartbeat 演示程序
 * 
 * 本 Demo 演示 CM4 和 DSP 之间的握手流程及周期性 Heartbeat 通信：
 * 
 * 1. CM4 初始化外设（时钟、UART、Mailbox）
 * 2. CM4 初始化 DSP PLL 并启动 DSP 核
 * 3. CM4 发送握手消息，等待 DSP 回复
 * 4. 握手完成后：
 *    - CM4 由 SysTick 定时器触发打印 Heartbeat
 *    - CM4 同时通过 Mailbox 触发 DSP 打印 Heartbeat
 * 
 * @note DSP 端需要运行对应的握手演示程序
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "board.h"
#include "s300.h"
#include "rcc.h"
#include "gpio.h"
#include "uart.h"
#include "mailbox.h"
#include "handshake_proto.h"

/*===========================================================================
 * 配置参数
 *===========================================================================*/

/** CM4 Heartbeat 打印间隔 (ms) */
#ifndef CM4_HEARTBEAT_INTERVAL_MS
#define CM4_HEARTBEAT_INTERVAL_MS     1000u
#endif

/** DSP Heartbeat 触发间隔 (ms) */
#ifndef DSP_HEARTBEAT_TRIGGER_MS
#define DSP_HEARTBEAT_TRIGGER_MS      2000u
#endif

/*===========================================================================
 * 全局变量
 *===========================================================================*/

/** SysTick 计数器 (ms) */
static volatile uint32_t g_tick_ms = 0;

/** CM4 Heartbeat 序列号 */
static uint32_t g_cm4_heartbeat_seq = 0;

/** DSP Heartbeat 触发序列号 */
static uint16_t g_dsp_trigger_seq = 0;

/** 握手状态 */
static HandshakeState_t g_handshake_state = HANDSHAKE_STATE_IDLE;

/*===========================================================================
 * SysTick 中断
 *===========================================================================*/

/**
 * @brief SysTick 中断处理函数
 */
void SysTick_Handler(void)
{
    g_tick_ms++;
}

/**
 * @brief 获取当前时间 (ms)
 */
static uint32_t millis(void)
{
    return g_tick_ms;
}

/**
 * @brief 简单延时
 */
static void delay_ms(uint32_t ms)
{
    uint32_t start = millis();
    while ((millis() - start) < ms) {
        __NOP();
    }
}

/*===========================================================================
 * DSP 控制
 *===========================================================================*/

/**
 * @brief 初始化 DSP PLL 和时钟
 *
 * @return 0 成功, <0 失败
 */
static int dsp_clock_init(void)
{
    int ret;

    /* 初始化 DSP PLL：24MHz / 6 * 800 / 2 / 2 / 2 = 400MHz */
    ret = rcc_init_dsp_pll(6, 800, 0, 2, 2);
    if (ret != RCC_STATUS_OK) {
        printf("[CM4] DSP PLL init failed: %d\r\n", ret);
        return ret;
    }

    printf("[CM4] DSP PLL initialized (400MHz)\r\n");
    return 0;
}

/*===========================================================================
 * DSP 外设初始化 (由 CM4 代为初始化)
 *===========================================================================*/

/** DSP 调试串口配置 (UART3: PA26/PA27) */
#define DSP_DEBUG_UART_IDX          3
#define DSP_DEBUG_UART_BAUDRATE     115200
#define DSP_DEBUG_UART_PORT         GPIOA
#define DSP_DEBUG_UART_TX_PIN       27
#define DSP_DEBUG_UART_RX_PIN       26
#define DSP_DEBUG_UART_FUNCTION     FUNCTION_3

/**
 * @brief 初始化 DSP 调试串口 (UART3)
 * 
 * DSP 无法直接访问 CM4 的 APB1 外设时钟控制器，
 * 因此需要 CM4 代为初始化 UART3。
 */
static void dsp_uart_init(void)
{
    /* 开启 UART3 时钟 */
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
    
    /* 配置 UART3 引脚 */
    set_gpio_function(DSP_DEBUG_UART_PORT, DSP_DEBUG_UART_TX_PIN, DSP_DEBUG_UART_FUNCTION);
    set_gpio_function(DSP_DEBUG_UART_PORT, DSP_DEBUG_UART_RX_PIN, DSP_DEBUG_UART_FUNCTION);
    
    /* 初始化 UART3 */
    init_uart(DSP_DEBUG_UART_IDX, UARTTYPE_STD_SERIAL, 
              rcc_get_clock(RCC_CLOCK_APB1), DSP_DEBUG_UART_BAUDRATE);

    /* 这条日志仍然走 CM4 的调试串口（gimbal_master 上默认是 UART2） */
    printf("[CM4] UART3 initialized for DSP (115200 baud)\r\n");
}

/**
 * @brief 启动 DSP 核
 * 
 * 启动流程：
 *   1. 触发 DSP warm reset (自弹起，仅复位内核)
 *   2. 等待 DSP 时钟稳定
 * 
 * @note DSP 程序需要预先加载到 DSP 内存中（通过 Flash Boot 或调试器）
 * @note DSP 域复位/解复位由 GDB 脚本负责，不在 CM4 代码中控制
 */
static void dsp_start(void)
{
    /* 触发 DSP warm reset (自弹起，仅复位内核，让 DSP 从头开始执行) */
    rcc_set_dsp_warm_reset(true);
    
    /* warm reset 是自弹起的，无需手动释放 */
    for (volatile int i = 0; i < 50000; i++) {
        __NOP();
    }
    
    printf("[CM4] DSP warm reset triggered\r\n");
}

/**
 * @brief 为本轮握手清空邮箱状态
 *
 * GDB 先解域复位、CM4 再触发 warm reset 的流程下，DSP 可能经历两次启动。
 * 在正式握手前清空 CM4<->DSP 双向 FIFO，避免上一轮启动残留消息干扰本轮握手。
 */
static void handshake_prepare_mailbox(void)
{
    (void)init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    (void)init_mailbox(DSP_MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    printf("[CM4] Mailbox re-synced for handshake\r\n");
}

/**
 * @brief 读取 DSP 发给 CM4 的消息
 *
 * 当前握手 demo 已实测确认：CM4 侧从 MAILBOX_BASE 收到 DSP 的 ACK 和
 * HEARTBEAT_REPLY，因此这里固定只保留该接收路径。
 *
 * @param out_msg 输出消息
 * @return 0 读到消息, -1 没有消息
 */
static int read_dsp_message(uint32_t *out_msg)
{
    if (out_msg == NULL) {
        return -1;
    }

    if (mailbox_sta_empty_flag_is(MAILBOX_BASE, 0) == 0) {
        *out_msg = read_mailbox(MAILBOX_BASE);
        return 0;
    }

    return -1;
}

/*===========================================================================
 * 握手流程
 *===========================================================================*/

/**
 * @brief 执行与 DSP 的握手流程
 * 
 * CM4 周期性发送 HANDSHAKE_INIT，等待 DSP 回复 HANDSHAKE_ACK。
 * 
 * @return 0 成功, <0 超时或失败
 */
static int handshake_with_dsp(void)
{
    uint32_t start_ms = millis();
    uint32_t next_send_ms = start_ms;

    handshake_prepare_mailbox();
    
    g_handshake_state = HANDSHAKE_STATE_WAITING;
    printf("[CM4] Waiting for DSP handshake...\r\n");
    
    while ((millis() - start_ms) < HANDSHAKE_TIMEOUT_MS) {
        uint32_t now_ms = millis();
        
        /* 周期性发送握手消息 */
        if ((int32_t)(now_ms - next_send_ms) >= 0) {
            int ret = write_mailbox(MAILBOX_BASE, HANDSHAKE_MSG_INIT);
            if (ret == 0) {
                printf("[CM4] Sent HANDSHAKE_INIT (0x%08lX)\r\n", 
                       (unsigned long)HANDSHAKE_MSG_INIT);
            }
            next_send_ms = now_ms + HANDSHAKE_RETRY_INTERVAL_MS;
        }
        
        /* 检查 DSP 回复 */
        {
            uint32_t msg;
            if (read_dsp_message(&msg) == 0) {
                printf("[CM4] Received from DSP: 0x%08lX (base=0x%08lX)\r\n",
                       (unsigned long)msg,
                       (unsigned long)MAILBOX_BASE);
            
                if (msg == HANDSHAKE_MSG_ACK) {
                    g_handshake_state = HANDSHAKE_STATE_DONE;
                    printf("[CM4] Handshake ACK received! Handshake completed.\r\n");
                    return 0;
                }
            
                /* DSP 也可能先发送 INIT（双方同时启动的情况） */
                if (msg == HANDSHAKE_MSG_INIT) {
                    /* 回复 ACK */
                    (void)write_mailbox(MAILBOX_BASE, HANDSHAKE_MSG_ACK);
                    g_handshake_state = HANDSHAKE_STATE_DONE;
                    printf("[CM4] Received DSP INIT, sent ACK. Handshake completed.\r\n");
                    return 0;
                }
            }
        }
    }
    
    g_handshake_state = HANDSHAKE_STATE_TIMEOUT;
    printf("[CM4] Handshake timeout! (%u ms)\r\n", (unsigned)HANDSHAKE_TIMEOUT_MS);
    return -1;
}

/*===========================================================================
 * Heartbeat 处理
 *===========================================================================*/

/**
 * @brief CM4 打印 Heartbeat（由 SysTick 触发）
 */
static void cm4_print_heartbeat(void)
{
    printf("[CM4] Heartbeat #%lu (tick=%lu ms)\r\n",
           (unsigned long)g_cm4_heartbeat_seq,
           (unsigned long)millis());
    g_cm4_heartbeat_seq++;
}

/**
 * @brief 触发 DSP 打印 Heartbeat（通过 Mailbox）
 */
static void trigger_dsp_heartbeat(void)
{
    uint32_t msg = HEARTBEAT_MAKE_TRIGGER(g_dsp_trigger_seq);
    
    int ret = write_mailbox(MAILBOX_BASE, msg);
    if (ret == 0) {
        printf("[CM4] Sent HEARTBEAT_TRIGGER seq=%u\r\n", g_dsp_trigger_seq);
        g_dsp_trigger_seq++;
    } else {
        printf("[CM4] Failed to send HEARTBEAT_TRIGGER\r\n");
    }
}

/**
 * @brief 检查并处理 DSP 的消息
 */
static void process_dsp_messages(void)
{
    while (1) {
        uint32_t msg;

        if (read_dsp_message(&msg) != 0) {
            break;
        }
        
        if (msg == HANDSHAKE_MSG_ACK) {
            printf("[CM4] Received late HANDSHAKE_ACK (base=0x%08lX)\r\n",
                   (unsigned long)MAILBOX_BASE);
        } else if (HEARTBEAT_IS_REPLY(msg)) {
            uint8_t seq = HEARTBEAT_REPLY_GET_SEQ(msg);
            uint8_t status = HEARTBEAT_REPLY_GET_STATUS(msg);
            printf("[CM4] Received DSP HEARTBEAT_REPLY: seq=%u, status=%u (base=0x%08lX)\r\n",
                   seq, status, (unsigned long)MAILBOX_BASE);
        } else {
            printf("[CM4] Received unknown DSP message: 0x%08lX (base=0x%08lX)\r\n",
                   (unsigned long)msg,
                   (unsigned long)MAILBOX_BASE);
        }
    }
}

/*===========================================================================
 * 主函数
 *===========================================================================*/

int main(void)
{
    uint32_t last_cm4_hb_ms = 0;
    uint32_t last_dsp_trigger_ms = 0;
    
    /* 板级初始化（时钟、UART 等） */
    board_init();
    
    /* 配置 SysTick (1ms 中断) */
    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000u) != 0u) {
        printf("[CM4] SysTick config failed!\r\n");
        while (1) {
            __NOP();
        }
    }
    
    printf("\r\n");
    printf("============================================\r\n");
    printf("  S300 CM4-DSP Handshake & Heartbeat Demo\r\n");
    printf("============================================\r\n");
    printf("[CM4] SystemCoreClock = %lu Hz\r\n", (unsigned long)SystemCoreClock);
    
    /* 初始化 CM4 侧 Mailbox */
    /* MAILBOX_BASE: CM4 -> DSP */
    /* DSP_MAILBOX_BASE: DSP -> CM4 (CM4 接收侧也需要初始化) */
    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    init_mailbox(DSP_MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    printf("[CM4] Mailbox initialized\r\n");
    
    /* 第二步：配置 DSP PLL */
    if (dsp_clock_init() != 0) {
        printf("[CM4] DSP clock init failed, halting.\r\n");
        while (1) {
            __NOP();
        }
    }

    /* 第三步：初始化 DSP 使用的外设 (UART3) */
    /* 注意：DSP 无法访问 APB1 时钟控制器，需 CM4 代为初始化 */
    dsp_uart_init();
    
    /* 第四步：触发 DSP warm reset，启动 DSP 核 */
    /* 注意：DSP 域解复位由 GDB 脚本负责，DSP Mailbox 仍由 DSP 端自行初始化 */
    dsp_start();
    
    /* 短暂延时，等待 DSP 初始化 (Mailbox) */
    delay_ms(100);
    
    /* 第五步：执行握手 */
    if (handshake_with_dsp() != 0) {
        printf("[CM4] Handshake failed! Entering error loop.\r\n");
        while (1) {
            delay_ms(1000);
            printf("[CM4] ERROR: DSP not responding\r\n");
        }
    }
    
    printf("\r\n[CM4] === Both cores ready, starting heartbeat ===\r\n\r\n");
    
    /* 主循环：周期性 Heartbeat */
    last_cm4_hb_ms = millis();
    last_dsp_trigger_ms = millis();
    
    while (1) {
        uint32_t now_ms = millis();
        
        /* CM4 Heartbeat（由 SysTick 时间触发） */
        if ((now_ms - last_cm4_hb_ms) >= CM4_HEARTBEAT_INTERVAL_MS) {
            cm4_print_heartbeat();
            last_cm4_hb_ms = now_ms;
        }
        
        /* 触发 DSP Heartbeat（通过 Mailbox） */
        if ((now_ms - last_dsp_trigger_ms) >= DSP_HEARTBEAT_TRIGGER_MS) {
            trigger_dsp_heartbeat();
            last_dsp_trigger_ms = now_ms;
        }
        
        /* 处理 DSP 消息 */
        process_dsp_messages();
    }
    
    return 0;
}
