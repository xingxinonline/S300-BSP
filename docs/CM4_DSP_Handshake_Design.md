# CM4-DSP 握手与心跳通信设计文档

## 文档版本

| 版本 | 日期       | 作者    | 说明                                                                      |
| ---- | ---------- | ------- | ------------------------------------------------------------------------- |
| 1.0  | 2026-03-09 | Copilot | 初始版本                                                                  |
| 1.1  | 2026-03-09 | Copilot | 更新职责分工：DSP 负责 PLL/Mailbox，CM4 负责 UART3                        |
| 1.2  | 2026-03-09 | Copilot | 恢复为 CM4 负责 DSP PLL，DSP 负责 DSP 侧 Mailbox                          |
| 1.3  | 2026-03-09 | Copilot | 统一最终流程：GDB 管域复位，CM4 管 PLL/UART3/warm reset，DSP 采用幂等握手 |
| 1.4  | 2026-03-09 | Copilot | 按实测结果更新 Mailbox 地址模型：CM4 侧 0x40019000，DSP 侧 0x44080400     |

---

## 1. 概述

本文档描述 S300 芯片上 CM4 核与 DSP 核之间的握手流程和周期性心跳通信的完整实现方案。

### 1.1 设计目标

1. **可靠启动**：CM4 作为主控核，控制 DSP 的启动时序
2. **握手同步**：确保双方都准备就绪后再开始工作
3. **心跳监测**：周期性通信，验证双方都在正常运行

### 1.2 职责分工

| 模块                 | CM4 负责              | DSP 负责     |
| -------------------- | --------------------- | ------------ |
| DSP 域复位/解复位    | GDB 调试脚本          |              |
| DSP PLL 初始化       | ✓                     |              |
| CM4 侧 Mailbox       | ✓                     |              |
| DSP 侧 Mailbox       |                       | ✓            |
| UART3 (DSP 调试串口) | ✓ (DSP 无法访问 APB1) |              |
| 握手发起             | ✓                     |              |
| 握手响应             |                       | ✓ (幂等 ACK) |

> **注意 1**：DSP 无法直接访问 CM4 的 APB1 外设时钟控制器，因此 UART3 必须由 CM4 代为初始化。
>
> **注意 2**：当前调试流程中，DSP 域复位/解复位由 GDB 脚本负责，CM4 代码不再直接写 `DSP_CEVA_RST_CTRL`。
>
> **注意 3**：由于保留了 “GDB 解域复位 + CM4 触发 warm reset” 两个逻辑，DSP 侧握手必须实现为幂等流程，能够对重复的 `HANDSHAKE_INIT` 重发 `HANDSHAKE_ACK`。

### 1.3 文件位置

| 文件                                                    | 说明                     |
| ------------------------------------------------------- | ------------------------ |
| `Algorithm_Models/protocol/handshake_proto.h`           | 协议定义（CM4/DSP 共用） |
| `Projects/Demo/Handshake_Heartbeat_Demo/Src/main.c`     | CM4 端实现               |
| `Projects/Demo/Handshake_Heartbeat_Demo/algorithm_reference/` | DSP 端参考实现           |

---

## 2. 硬件架构

### 2.1 双核架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           S300 SoC                                      │
├─────────────────────────────┬───────────────────────────────────────────┤
│        CM4 域               │              DSP 域                       │
│   (Cortex-M4 @ 192MHz)      │      (CEVA SensPro250 @ 400MHz)           │
│                             │                                           │
│  ┌─────────────────────┐    │    ┌─────────────────────┐                │
│  │ SRAM (256KB)        │    │    │ PTCM (程序存储器)    │                │
│  └─────────────────────┘    │    └─────────────────────┘                │
│                             │                                           │
│  ┌─────────────────────┐    │    ┌─────────────────────┐                │
│  │ Mailbox 外设        │◄───┼───►│ Mailbox 外设        │                │
│  │ 0x40019000          │    │    │ 0x44080400          │                │
│  │ WRDATA: M4→DSP      │    │    │ WRDATA: DSP→M4      │                │
│  │ RDDATA: DSP→M4      │    │    │ RDDATA: M4→DSP      │                │
│  └─────────────────────┘    │    └─────────────────────┘                │
└─────────────────────────────┴───────────────────────────────────────────┘
```

### 2.2.1 实测结论

本次握手与 heartbeat 联调已经确认：

1. CM4 侧稳定接收 DSP `HANDSHAKE_ACK` 和 `HEARTBEAT_REPLY` 的基址是 `MAILBOX_BASE = 0x40019000`
2. DSP 侧应使用 `DSP_MAILBOX_BASE = 0x44080400` 访问同一个 Mailbox 外设
3. 旧版文档中的 `0x40060000 / 0x40070000` 双基址模型不适用于当前 demo 的实测链路

### 2.2 关键寄存器

| 寄存器              | 地址       | 功能                  |
| ------------------- | ---------- | --------------------- |
| `DSP_CEVA_RST_CTRL` | 0x4000A018 | DSP 域复位控制        |
| `DSP_WARM_RSTN`     | 0x4000A010 | DSP 内核 warm reset   |
| `DSP_PLL_CTRL`      | 0x4000A01C | DSP PLL 控制          |
| `DSP_PLL_CTRL2`     | 0x4000A020 | DSP PLL 控制2         |
| `MAILBOX_BASE`      | 0x40019000 | CM4 视角 Mailbox 外设 |
| `DSP_MAILBOX_BASE`  | 0x44080400 | DSP 视角 Mailbox 外设 |

### 2.3 两种复位的区别

```
┌────────────────────────────────────────────────────────────────────────┐
│                        DSP 复位机制                                     │
├─────────────────────────────┬──────────────────────────────────────────┤
│ 域复位 (Domain Reset)       │ Warm Reset                               │
│ DSP_CEVA_RST_CTRL=0         │ DSP_WARM_RSTN=0                          │
├─────────────────────────────┼──────────────────────────────────────────┤
│ • 复位整个 DSP 时钟域        │ • 仅复位 DSP 内核                         │
│ • 包括 PLL、外设、内存控制器  │ • 保持时钟域运行                          │
│ • 需要手动释放               │ • 自弹起，无需手动释放                     │
│ • 用于初始启动               │ • 用于重启 DSP 程序                        │
└─────────────────────────────┴──────────────────────────────────────────┘
```

---

## 3. 启动时序

### 3.1 完整启动流程

```
时间 ──────────────────────────────────────────────────────────────────────►

GDB                    CM4                                              DSP
 │                      │                                                │
 │ [1] DSP_CEVA_RST_CTRL=0                                               │
 │     保持 DSP 域复位                                                   │
 │                      │                                                │
 │ [2] 加载 DSP bin 到 PTCM/DTCM                                          │
 │                      │                                                │
 │ [3] DSP_CEVA_RST_CTRL=1                                               │
 │     释放 DSP 域复位                                                   │
 │                      │                                                │
 │                      │ [4] board_init()                               │
 │                      │     - 时钟初始化                                │
 │                      │     - UART2 作为 CM4 printf                     │
 │                      │                                                │
 │                      │ [5] SysTick, CM4 双向 Mailbox 初始化            │
 │                      │                                                │
 │                      │ [6] dsp_clock_init()                           │
 │                      │     配置 DSP PLL (400MHz)                       │
 │                      │                                                │
 │                      │ [7] dsp_uart_init()                            │
 │                      │     开启 UART3 时钟 (APB1)                      │
 │                      │     配置 UART3 引脚 (PA27/PA26)                 │
 │                      │     初始化 UART3 (115200)                       │
 │                      │                                                │
 │                      │ [8] dsp_start()                                │
 │                      │     DSP_WARM_RSTN = 0 (warm reset)             │
 │                      │                                                │ [A] DSP 可能已因 GDB 解域复位启动过一次
 │                      │                                                │ [B] DSP 再次因 warm reset 重新启动
 │                      │                                                │     从复位向量执行正式流程
 │                      │ [9] 等待 100ms                                 │
 │                      │                                                │ [C] dsp_mailbox_init()
 │                      │                                                │     初始化 DSP 侧 Mailbox
 │                      │                                                │
 │                      │ [10] 重新初始化 CM4 双向 Mailbox                │
 │                      │      清理上一轮启动残留 FIFO                    │
 │                      │                                                │
 │                      │ [11] ─── HANDSHAKE_INIT ──────────────────────►│
 │                      │                                                │ [D] 收到 INIT，回复 ACK
 │                      │ ◄── HANDSHAKE_ACK ────────────────────────────│
 │                      │                                                │
 │                      │ [12] 握手完成                                   │ [E] 握手完成
 │                      │      开始周期性 Heartbeat                       │     若再次收到 INIT，仍重发 ACK
 ▼                      ▼                                                ▼
```

### 3.2 CM4 端伪代码

```c
int main(void)
{
    /* Step 1: 板级初始化 */
    board_init();

    /* Step 2: CM4 初始化 mailbox 接收/发送两侧 */
    SysTick_Config(SystemCoreClock / 1000);
    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    init_mailbox(DSP_MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    
    /* Step 3: 配置 DSP PLL */
    rcc_init_dsp_pll(6, 800, 0, 2, 2);  // 400MHz

    /* Step 4: 初始化 DSP 使用的外设 (UART3) */
    /* 注意: DSP 无法访问 APB1 时钟控制器，需 CM4 代为初始化 */
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
    set_gpio_function(GPIOA, 27, FUNCTION_3);  // UART3_TX
    set_gpio_function(GPIOA, 26, FUNCTION_3);  // UART3_RX
    init_uart(3, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), 115200);

    /* Step 5: 触发 DSP warm reset */
    /* 注意: DSP 域复位/解复位已由 GDB 脚本处理 */
    rcc_set_dsp_warm_reset(true); // 触发 warm reset (自弹起)
    delay_us(50000);
    
    /* Step 6: 等待 DSP 初始化 (Mailbox) */
    delay_ms(100);

    /* Step 7: 为本轮握手重新同步 mailbox */
    init_mailbox(MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    init_mailbox(DSP_MAILBOX_BASE, 4, MAILBOX_IRQ_NONE);
    
    /* Step 8: 握手 */
    handshake_with_dsp();
    
    /* 主循环: Heartbeat */
    while (1) {
        /* CM4 自己的 heartbeat (SysTick 触发) */
        if (timer_elapsed(1000ms)) {
            printf("[CM4] Heartbeat #%d\n", seq++);
        }
        
        /* 触发 DSP heartbeat (Mailbox) */
        if (timer_elapsed(2000ms)) {
            write_mailbox(MAILBOX_BASE, HEARTBEAT_MAKE_TRIGGER(seq));
        }
        
        /* 处理 DSP 回复 */
        process_dsp_messages();
    }
}
```

### 3.3 DSP 端伪代码

```c
int main(void)
{
    /* DSP 可能经历两次启动：
     * 1. GDB 释放 domain reset 后的一次启动
     * 2. CM4 触发 warm reset 后的正式启动
     *
     * 因此下面流程必须设计为幂等。
     */

    /* Step A: 基础初始化 */
    dsp_basic_init();
    
    /* Step B: 初始化 DSP 侧 Mailbox */
    /* 注意: DSP PLL 已由 CM4 完成配置 */
    dsp_mailbox_init();
    
    /* UART3 已由 CM4 初始化，DSP 可直接使用 */
    printf("[DSP] Started, PLL=%dMHz\r\n", 400);
    
    /* Step C: 等待握手 */
    while (1) {
        if (mailbox_has_data()) {
            uint32_t msg = mailbox_read();
            
            if (msg == HANDSHAKE_MSG_INIT) {
                /* 收到握手请求，回复 ACK */
                mailbox_write(HANDSHAKE_MSG_ACK);
                printf("[DSP] Handshake done!\r\n");
                break;  // 退出握手循环
            }
        }
    }
    
    /* Step D: 主循环 */
    while (1) {
        if (mailbox_has_data()) {
            uint32_t msg = mailbox_read();
            
            if (msg == HANDSHAKE_MSG_INIT) {
                /* 幂等握手：若 CM4 因重启/清 FIFO 未收到 ACK，则重发 ACK */
                mailbox_write(HANDSHAKE_MSG_ACK);
            } else if (HEARTBEAT_IS_TRIGGER(msg)) {
                uint16_t seq = HEARTBEAT_GET_SEQ(msg);
                printf("[DSP] Heartbeat #%d (triggered by CM4)\r\n", count++);
                
                /* 回复 (使用正确格式!) */
                mailbox_write(HEARTBEAT_MAKE_REPLY(seq, DSP_STATUS_OK));
            }
        }
        
        /* 其他 DSP 任务... */
    }
}
```

> **重要提醒**:
> 1. DSP 不再负责 PLL 初始化，PLL 由 CM4 配置
> 2. DSP 必须自己初始化 DSP 侧 Mailbox
> 3. DSP 必须把握手实现成幂等：主循环中再次收到 `HANDSHAKE_INIT` 时仍然重发 `HANDSHAKE_ACK`
> 4. Heartbeat 回复必须使用 `HEARTBEAT_MAKE_REPLY()` 格式，不要原样返回 TRIGGER

---

## 4. 协议定义

### 4.1 消息格式

所有消息均为 32 位无符号整数。

### 4.2 握手消息

| 消息名               | 方向      | 值           | 说明           |
| -------------------- | --------- | ------------ | -------------- |
| `HANDSHAKE_MSG_INIT` | CM4 → DSP | `0x5A5A5A5A` | 握手初始化请求 |
| `HANDSHAKE_MSG_ACK`  | DSP → CM4 | `0xA5A5A5A5` | 握手确认响应   |

### 4.3 Heartbeat 消息

#### Heartbeat Trigger (CM4 → DSP)

```
┌────────────────┬────────────────────────────────┐
│  Bit [31:16]   │  Bit [15:0]                    │
│  类型: 0xBEA7  │  序列号 (0~65535 循环)          │
└────────────────┴────────────────────────────────┘
```

| 操作       | 宏定义                        |
| ---------- | ----------------------------- |
| 构造消息   | `HEARTBEAT_MAKE_TRIGGER(seq)` |
| 检查类型   | `HEARTBEAT_IS_TRIGGER(msg)`   |
| 提取序列号 | `HEARTBEAT_GET_SEQ(msg)`      |

#### Heartbeat Reply (DSP → CM4)

```
┌────────────────┬──────────┬──────────┐
│  Bit [31:20]   │ Bit[15:8]│ Bit[7:0] │
│  类型: 0xACE   │  seq     │  status  │
└────────────────┴──────────┴──────────┘
```

| 操作       | 宏定义                              |
| ---------- | ----------------------------------- |
| 构造消息   | `HEARTBEAT_MAKE_REPLY(seq, status)` |
| 检查类型   | `HEARTBEAT_IS_REPLY(msg)`           |
| 提取序列号 | `HEARTBEAT_REPLY_GET_SEQ(msg)`      |
| 提取状态   | `HEARTBEAT_REPLY_GET_STATUS(msg)`   |

### 4.4 状态码

```c
typedef enum {
    DSP_STATUS_OK    = 0,   /* 正常 */
    DSP_STATUS_BUSY  = 1,   /* 忙碌 */
    DSP_STATUS_ERROR = 2,   /* 错误 */
} DspStatus_t;
```

---

## 5. DSP 端实现指南

### 5.1 Mailbox 地址配置

DSP 视角的 Mailbox 地址与 CM4 不同。当前 demo 实测有效的地址模型如下：

```c
/* DSP 视角的 Mailbox 地址 */
#define DSP_MAILBOX_BASE        0x44080400u

/* 寄存器偏移 */
#define MAILBOX_WRDATA_OFFSET   0x00   /* DSP 写入 -> CM4 读取 */
#define MAILBOX_RDDATA_OFFSET   0x08   /* DSP 读取 <- CM4 写入 */
#define MAILBOX_STA_OFFSET      0x10   /* 状态寄存器 */
#define MAILBOX_CTRL_OFFSET     0x2C   /* 控制寄存器 */

/* 状态位 */
#define MAILBOX_STA_EMPTY       (1u << 0)
#define MAILBOX_STA_FULL        (1u << 1)
```

### 5.2 Mailbox 底层函数

```c
/* 初始化 Mailbox */
void dsp_mailbox_init(void)
{
    volatile uint32_t *ctrl = (volatile uint32_t *)(DSP_MAILBOX_BASE + MAILBOX_CTRL_OFFSET);

    /* 单 Mailbox 外设中同时清空 TX/RX FIFO */
    *ctrl = 0x03;
}

/* 检查是否有数据可读 */
int dsp_mailbox_has_data(void)
{
    volatile uint32_t *sta = (volatile uint32_t *)(DSP_MAILBOX_BASE + MAILBOX_STA_OFFSET);
    return (*sta & MAILBOX_STA_EMPTY) == 0;
}

/* 读取一个 32 位数据 */
uint32_t dsp_mailbox_read(void)
{
    volatile uint32_t *fifo = (volatile uint32_t *)(DSP_MAILBOX_BASE + MAILBOX_RDDATA_OFFSET);
    return *fifo;
}

/* 写入一个 32 位数据 */
int dsp_mailbox_write(uint32_t data)
{
    volatile uint32_t *sta = (volatile uint32_t *)(DSP_MAILBOX_BASE + MAILBOX_STA_OFFSET);
    volatile uint32_t *fifo = (volatile uint32_t *)(DSP_MAILBOX_BASE + MAILBOX_WRDATA_OFFSET);
    
    /* 检查 FIFO 是否满 */
    if (*sta & MAILBOX_STA_FULL) {
        return -1;
    }
    
    *fifo = data;
    return 0;
}
```

### 5.3 完整 DSP 主程序模板

```c
/**
 * @file dsp_main.c
 * @brief DSP 端握手与心跳实现
 */

#include <stdio.h>
#include <stdint.h>

/* 包含共享协议头文件 */
#include "handshake_proto.h"

/*===========================================================================
 * 配置
 *===========================================================================*/
#define DSP_MAILBOX_BASE        0x44080400u
#define MAILBOX_WRDATA_OFFSET   0x00
#define MAILBOX_RDDATA_OFFSET   0x08
#define MAILBOX_STA_OFFSET      0x10
#define MAILBOX_STA_EMPTY       (1u << 0)
#define MAILBOX_STA_FULL        (1u << 1)

/*===========================================================================
 * Mailbox 操作
 *===========================================================================*/
static void dsp_mailbox_init(void)
{
    /* 可选：清空 FIFO */
    printf("[DSP] Mailbox initialized\r\n");
}

static int dsp_mailbox_has_data(void)
{
    volatile uint32_t sta = *(volatile uint32_t *)(DSP_MAILBOX_BASE + MAILBOX_STA_OFFSET);
    return (sta & MAILBOX_STA_EMPTY) == 0;
}

static uint32_t dsp_mailbox_read(void)
{
    return *(volatile uint32_t *)(DSP_MAILBOX_BASE + MAILBOX_RDDATA_OFFSET);
}

static int dsp_mailbox_write(uint32_t data)
{
    volatile uint32_t sta = *(volatile uint32_t *)(DSP_MAILBOX_BASE + MAILBOX_STA_OFFSET);
    if (sta & MAILBOX_STA_FULL) {
        return -1;
    }
    *(volatile uint32_t *)(DSP_MAILBOX_BASE + MAILBOX_WRDATA_OFFSET) = data;
    return 0;
}

/*===========================================================================
 * 握手处理
 *===========================================================================*/
static void dsp_send_handshake_ack(void)
{
    dsp_mailbox_write(HANDSHAKE_MSG_ACK);
}

static int dsp_wait_handshake(void)
{
    printf("[DSP] Waiting for CM4 handshake...\r\n");
    
    while (1) {
        if (dsp_mailbox_has_data()) {
            uint32_t msg = dsp_mailbox_read();
            printf("[DSP] Received: 0x%08lX\r\n", (unsigned long)msg);
            
            if (msg == HANDSHAKE_MSG_INIT) {
                /* 回复 ACK */
                dsp_send_handshake_ack();
                printf("[DSP] Sent ACK, handshake done!\r\n");
                return 0;
            }
        }
        
        /* 简单延时 */
        for (volatile int i = 0; i < 10000; i++);
    }
}

/*===========================================================================
 * Heartbeat 处理
 *===========================================================================*/
static uint32_t g_heartbeat_count = 0;

static void dsp_handle_heartbeat(uint32_t msg)
{
    uint16_t seq = HEARTBEAT_GET_SEQ(msg);
    
    /* 打印 Heartbeat */
    printf("[DSP] Heartbeat #%lu (trigger seq=%u)\r\n",
           (unsigned long)g_heartbeat_count++, seq);
    
    /* 回复 */
    uint32_t reply = HEARTBEAT_MAKE_REPLY((uint8_t)seq, DSP_STATUS_OK);
    dsp_mailbox_write(reply);
}

/*===========================================================================
 * 主函数
 *===========================================================================*/
int main(void)
{
    printf("\r\n========================================\r\n");
    printf("  S300 DSP Handshake & Heartbeat Demo\r\n");
    printf("========================================\r\n");

    /* PLL 已由 CM4 配置 */
    printf("[DSP] PLL initialized (400MHz)\r\n");

    /* 初始化 DSP 侧 Mailbox */
    dsp_mailbox_init();
    
    /* 等待握手 */
    if (dsp_wait_handshake() != 0) {
        printf("[DSP] Handshake failed!\r\n");
        while (1);
    }
    
    printf("[DSP] Ready, waiting for heartbeat triggers...\r\n");
    
    /* 主循环 */
    while (1) {
        if (dsp_mailbox_has_data()) {
            uint32_t msg = dsp_mailbox_read();
            
            if (msg == HANDSHAKE_MSG_INIT) {
                /* 幂等握手：重发 ACK，帮助 CM4 收敛 */
                dsp_send_handshake_ack();
            } else if (HEARTBEAT_IS_TRIGGER(msg)) {
                dsp_handle_heartbeat(msg);
            } else {
                printf("[DSP] Unknown msg: 0x%08lX\r\n", (unsigned long)msg);
            }
        }
        
        /* 短延时，避免忙等 */
        for (volatile int i = 0; i < 1000; i++);
    }
    
    return 0;
}
```

### 5.4 DSP 需要完善和迭代适配的内容

DSP 侧当前需要按下面的优先级进行适配，避免握手偶发失败或调试日志与实际状态不一致。

#### 5.4.1 必做项

1. **实现幂等握手**

   - 在 `dsp_wait_handshake()` 中收到 `HANDSHAKE_MSG_INIT` 后发送 `HANDSHAKE_MSG_ACK`
   - 在握手完成后的主循环中，如果再次收到 `HANDSHAKE_MSG_INIT`，仍然要再次发送 `HANDSHAKE_MSG_ACK`
   - 不要把重复的 `HANDSHAKE_MSG_INIT` 打成 `Unknown message`

2. **仅初始化 DSP 自己负责的资源**

   - 需要初始化：DSP 侧 Mailbox、DSP 自己的运行时环境、DSP 算法上下文
   - 不要重复初始化：DSP PLL、UART3 APB1 时钟、CM4 侧 Mailbox

3. **统一 heartbeat 回复格式**

   - 必须使用 `HEARTBEAT_MAKE_REPLY(seq, DSP_STATUS_OK)`
   - 不要回传原始 trigger 值 `0xBEA7xxxx`

4. **保证日志与状态一致**

   - `Handshake completed` 日志只能在 ACK 实际发送成功后输出
   - 若 `dsp_mailbox_write(HANDSHAKE_MSG_ACK)` 失败，应打印明确错误并继续等待下一次 `HANDSHAKE_INIT`

#### 5.4.2 建议项

1. **增加启动阶段标识**

   建议 DSP 在启动日志中区分：
   - `boot after domain release`
   - `boot after warm reset`

   这样可帮助区分是 GDB 解域复位带来的第一次启动，还是 CM4 warm reset 带来的正式启动。

2. **增加 mailbox 自检日志**

   建议在 `dsp_mailbox_init()` 后打印：
   - mailbox 基地址
   - FIFO 清空结果
   - 首次状态寄存器值

3. **增加 ACK 重发统计**

   建议记录：
   - 首次握手 ACK 次数
   - 主循环中幂等 ACK 重发次数

   这能帮助判断当前系统是否存在“CM4 清 FIFO 导致首次 ACK 丢失”的情况。

#### 5.4.3 推荐实现骨架

```c
static bool g_handshake_done = false;
static uint32_t g_ack_resend_count = 0;

static int dsp_send_handshake_ack_logged(void)
{
    int ret = dsp_mailbox_write(HANDSHAKE_MSG_ACK);
    if (ret == 0) {
        printf("[DSP] Sent HANDSHAKE_ACK (0x%08lX)\r\n",
               (unsigned long)HANDSHAKE_MSG_ACK);
    } else {
        printf("[DSP][WARN] HANDSHAKE_ACK send failed: %d\r\n", ret);
    }
    return ret;
}

static void dsp_handle_message(uint32_t msg)
{
    if (msg == HANDSHAKE_MSG_INIT) {
        if (dsp_send_handshake_ack_logged() == 0) {
            if (g_handshake_done) {
                g_ack_resend_count++;
            }
            g_handshake_done = true;
        }
        return;
    }

    if (HEARTBEAT_IS_TRIGGER(msg)) {
        uint16_t seq = HEARTBEAT_GET_SEQ(msg);
        dsp_mailbox_write(HEARTBEAT_MAKE_REPLY(seq, DSP_STATUS_OK));
        return;
    }

    printf("[DSP] Unknown message: 0x%08lX\r\n", (unsigned long)msg);
}
```

### 5.5 DSP PLL 初始化详细流程

> 本节描述的是 CM4 侧对 DSP PLL 的配置流程，对应 BSP 中的 `rcc_init_dsp_pll()` 实现。

DSP 不需要在本 demo 中自行初始化 PLL；以下内容用于 DSP 团队理解 CM4 侧 PLL 配置流程，以及后续若需迁移到 DSP 自主启动模式时参考。

#### 5.5.1 寄存器定义

```c
/* DSP RCC 基地址 */
#define DSP_RCC_BASE            0x4000A000u

/* DSP PLL 相关寄存器 */
#define DSP_SYS_CLK_SEL         (*(volatile uint32_t *)(DSP_RCC_BASE + 0x0014u))
#define DSP_PLL_CTRL            (*(volatile uint32_t *)(DSP_RCC_BASE + 0x001Cu))
#define DSP_PLL_CTRL2           (*(volatile uint32_t *)(DSP_RCC_BASE + 0x0020u))
#define DSP_PLOCK_STATUS        (*(volatile uint32_t *)(DSP_RCC_BASE + 0x0024u))
```

#### 5.5.2 寄存器位域定义

**DSP_SYS_CLK_SEL (0x4000A014)** - 系统时钟选择

| Bit   | 名称    | 说明                            |
| ----- | ------- | ------------------------------- |
| [1:0] | CLK_SEL | 00: RC, 01: HSE(24MHz), 10: PLL |

**DSP_PLL_CTRL (0x4000A01C)** - PLL 控制寄存器 1

| Bit     | 名称   | 说明                  |
| ------- | ------ | --------------------- |
| [29:24] | REFDIV | 参考分频 (1-63)       |
| [23:0]  | FRAC   | 小数分频 (0=整数模式) |

**DSP_PLL_CTRL2 (0x4000A020)** - PLL 控制寄存器 2

| Bit     | 名称     | 说明                     |
| ------- | -------- | ------------------------ |
| [31]    | -        | 保留 (写 0)              |
| [30]    | -        | 保留 (写 0)              |
| [29]    | -        | 写 1                     |
| [28]    | -        | 保留 (写 0)              |
| [27]    | -        | 保留 (写 0)              |
| [26]    | -        | 保留 (写 0)              |
| [25]    | PLL_PD   | PLL 掉电: 1=掉电, 0=使能 |
| [17:15] | POSTDIV2 | 后分频 2 (1-7)           |
| [14:12] | POSTDIV1 | 后分频 1 (1-7)           |
| [11:0]  | FBDIV    | 反馈分频 (1-4095)        |

**DSP_PLOCK_STATUS (0x4000A024)** - PLL 锁定状态

| Bit | 名称         | 说明                   |
| --- | ------------ | ---------------------- |
| [1] | DSP_PLL_LOCK | DSP PLL 锁定: 1=已锁定 |

#### 5.5.3 频率计算公式

```
Fout = Fin × FBDIV / (REFDIV × POSTDIV1 × POSTDIV2)

示例 (400MHz):
  Fin = 24MHz (HSE)
  REFDIV = 6
  FBDIV = 800
  POSTDIV1 = 2
  POSTDIV2 = 2
  
  Fout = 24MHz × 800 / (6 × 2 × 2) = 24 × 800 / 24 = 800MHz
  
注意: 实际输出频率可能还有一个固定的 /2 分频
      800MHz / 2 = 400MHz
```

#### 5.5.4 完整初始化代码

```c
#include <stdint.h>

/* DSP RCC 寄存器定义 */
#define DSP_RCC_BASE            0x4000A000u
#define DSP_SYS_CLK_SEL         (*(volatile uint32_t *)(DSP_RCC_BASE + 0x0014u))
#define DSP_PLL_CTRL            (*(volatile uint32_t *)(DSP_RCC_BASE + 0x001Cu))
#define DSP_PLL_CTRL2           (*(volatile uint32_t *)(DSP_RCC_BASE + 0x0020u))
#define DSP_PLOCK_STATUS        (*(volatile uint32_t *)(DSP_RCC_BASE + 0x0024u))

/**
 * @brief 初始化 DSP PLL
 * 
 * @param refdiv   参考分频 (1-63)
 * @param fbdiv    反馈分频 (1-4095)
 * @param postdiv1 后分频1 (1-7)
 * @param postdiv2 后分频2 (1-7)
 * @return 0 成功, -1 PLL 锁定超时
 * 
 * @note 400MHz 配置: refdiv=6, fbdiv=800, postdiv1=2, postdiv2=2
 */
int dsp_pll_init(uint16_t refdiv, uint16_t fbdiv, uint16_t postdiv1, uint16_t postdiv2)
{
    uint32_t temp;
    uint32_t timeout;
    
    /* Step 1: 切换到 HSE (24MHz) 作为临时时钟源 */
    temp = DSP_SYS_CLK_SEL;
    temp &= ~0x3u;          /* 清除 [1:0] */
    temp |= 0x1u;           /* 设置为 01 = HSE */
    DSP_SYS_CLK_SEL = temp;
    
    /* 短延时等待时钟切换稳定 */
    for (volatile int i = 0; i < 1000; i++);
    
    /* Step 2: 配置 PLL_CTRL2 (含 PLL 掉电位) */
    temp = 0u
         | (0u << 31)                       /* 保留 */
         | (0u << 30)                       /* 保留 */
         | (1u << 29)                       /* 固定写 1 */
         | (0u << 28)                       /* 保留 */
         | (0u << 27)                       /* 保留 */
         | (0u << 26)                       /* 保留 */
         | (1u << 25)                       /* PLL_PD = 1 (先掉电) */
         | ((postdiv2 & 0x7u) << 15)        /* POSTDIV2 */
         | ((postdiv1 & 0x7u) << 12)        /* POSTDIV1 */
         | (fbdiv & 0x0FFFu);               /* FBDIV */
    DSP_PLL_CTRL2 = temp;
    
    /* Step 3: 配置 PLL_CTRL (REFDIV + FRAC) */
    temp = ((uint32_t)(refdiv & 0x3Fu) << 24)  /* REFDIV */
         | (0u & 0xFFFFFFu);                    /* FRAC = 0 (整数模式) */
    DSP_PLL_CTRL = temp;
    
    /* Step 4: 等待 PLL 锁定 */
    timeout = 100000u;
    while (timeout--) {
        if (DSP_PLOCK_STATUS & (1u << 1)) {  /* 检查 bit[1] DSP_PLL_LOCK */
            break;
        }
    }
    if (timeout == 0) {
        return -1;  /* PLL 锁定超时 */
    }
    
    /* Step 5: 清除 PLL 掉电位，使能 PLL */
    DSP_PLL_CTRL2 &= ~(1u << 25);
    
    /* Step 6: 切换系统时钟到 PLL */
    temp = DSP_SYS_CLK_SEL;
    temp &= ~0x3u;          /* 清除 [1:0] */
    temp |= 0x2u;           /* 设置为 10 = PLL */
    DSP_SYS_CLK_SEL = temp;
    
    /* 短延时等待时钟切换稳定 */
    for (volatile int i = 0; i < 1000; i++);
    
    return 0;
}

/* 使用示例 */
void dsp_init(void)
{
    /* 配置 DSP PLL 到 400MHz */
    /* 24MHz / 6 * 800 / 2 / 2 / 2 = 400MHz */
    if (dsp_pll_init(6, 800, 2, 2) != 0) {
        /* PLL 初始化失败处理 */
        while (1);
    }
    
    /* 继续其他初始化... */
}
```

#### 5.5.5 初始化流程图

```
┌────────────────────────────────────────────────────────────────┐
│                    DSP PLL 初始化流程                          │
└────────────────────────────────────────────────────────────────┘
                              │
                              ▼
         ┌─────────────────────────────────────────┐
         │ Step 1: 切换系统时钟到 HSE (24MHz)        │
         │ DSP_SYS_CLK_SEL[1:0] = 01                │
         └─────────────────────────────────────────┘
                              │
                              ▼
         ┌─────────────────────────────────────────┐
         │ Step 2: 配置 PLL_CTRL2                   │
         │ - PLL_PD = 1 (掉电状态配置)              │
         │ - POSTDIV2 = 2                           │
         │ - POSTDIV1 = 2                           │
         │ - FBDIV = 800                            │
         └─────────────────────────────────────────┘
                              │
                              ▼
         ┌─────────────────────────────────────────┐
         │ Step 3: 配置 PLL_CTRL                    │
         │ - REFDIV = 6                             │
         │ - FRAC = 0 (整数模式)                    │
         └─────────────────────────────────────────┘
                              │
                              ▼
         ┌─────────────────────────────────────────┐
         │ Step 4: 等待 PLL 锁定                    │
         │ 轮询 DSP_PLOCK_STATUS[1] == 1            │
         └─────────────────────────────────────────┘
                              │
                              ▼
         ┌─────────────────────────────────────────┐
         │ Step 5: 清除 PLL_PD，使能 PLL            │
         │ DSP_PLL_CTRL2[25] = 0                    │
         └─────────────────────────────────────────┘
                              │
                              ▼
         ┌─────────────────────────────────────────┐
         │ Step 6: 切换系统时钟到 PLL               │
         │ DSP_SYS_CLK_SEL[1:0] = 10                │
         └─────────────────────────────────────────┘
                              │
                              ▼
                         [ 完成 ]
```

---

## 6. 调试与验证

### 6.1 预期串口输出

#### CM4 端 (UART2)

```
============================================
  S300 CM4-DSP Handshake & Heartbeat Demo
============================================
[CM4] SystemCoreClock = 192000000 Hz
[CM4] Mailbox initialized
[CM4] Mailbox re-synced for handshake
[CM4] DSP PLL initialized (400MHz)
[CM4] UART3 initialized for DSP (115200 baud)
[CM4] DSP warm reset triggered
[CM4] Waiting for DSP handshake...
[CM4] Sent HANDSHAKE_INIT (0x5A5A5A5A)
[CM4] Received from DSP: 0xA5A5A5A5
[CM4] Handshake ACK received! Handshake completed.

[CM4] === Both cores ready, starting heartbeat ===

[CM4] Heartbeat #0 (tick=1200 ms)
[CM4] Heartbeat #1 (tick=2200 ms)
[CM4] Sent HEARTBEAT_TRIGGER seq=0
[CM4] Received DSP HEARTBEAT_REPLY: seq=0, status=0
[CM4] Heartbeat #2 (tick=3200 ms)
[CM4] Heartbeat #3 (tick=4200 ms)
[CM4] Sent HEARTBEAT_TRIGGER seq=1
[CM4] Received DSP HEARTBEAT_REPLY: seq=1, status=0
...
```

#### DSP 端 (UART3)

```
========================================
  S300 DSP Handshake & Heartbeat Demo
========================================
[DSP] PLL initialized (400MHz)
[DSP] Mailbox initialized
[DSP] Waiting for CM4 handshake...
[DSP] Received: 0x5A5A5A5A
[DSP] Sent HANDSHAKE_ACK (0xA5A5A5A5)
[DSP] Handshake completed!
[DSP] Ready, waiting for heartbeat triggers...
[DSP] Heartbeat #0 (trigger seq=0)
[DSP] Heartbeat #1 (trigger seq=1)
[DSP] Heartbeat #2 (trigger seq=2)
...
```

### 6.2 常见问题排查

| 现象                          | 可能原因                        | 解决方法                                                  |
| ----------------------------- | ------------------------------- | --------------------------------------------------------- |
| 握手超时                      | DSP 未启动                      | 检查 DSP 程序是否正确加载                                 |
| 握手超时                      | DSP PLL 未初始化                | 检查 CM4 是否已调用 `rcc_init_dsp_pll()`                  |
| 握手超时                      | Mailbox 地址错误                | 确认 DSP 端使用 `0x44080400`，CM4 侧接收落在 `0x40019000` |
| 握手超时                      | DSP Mailbox 未初始化            | DSP 端需自行初始化 DSP 侧 Mailbox                         |
| 握手超时                      | ACK 被首次启动残留干扰          | 检查 DSP 是否实现幂等 ACK 重发逻辑                        |
| DSP 启动日志重复              | 同时保留了解域复位和 warm reset | 属于当前预期，需靠幂等握手收敛                            |
| DSP 无打印输出                | UART3 未初始化                  | 确认 CM4 端已调用 `dsp_uart_init()`                       |
| DSP 打印乱码                  | 波特率不匹配                    | 确认 UART3 配置为 115200, 8N1                             |
| DSP 已发送 ACK 但 CM4 收不到  | DSP 仍使用旧双基址模型          | 切换到 `DSP_MAILBOX_BASE + WRDATA/RDDATA` 模型            |
| 无 Heartbeat 回复             | 回复格式错误                    | 使用 `HEARTBEAT_MAKE_REPLY()` 格式                        |
| `Unknown message: 0x5A5A5A5A` | DSP 未实现幂等握手              | 主循环中收到 `HANDSHAKE_MSG_INIT` 时也要重发 ACK          |
| 数据乱码                      | Mailbox 未初始化                | 确保双方都正确初始化各自的 Mailbox                        |

### 6.3 调试用 GDB 命令

```gdb
# 检查 DSP 域复位状态
x/1wx 0x4000a018
# 期望值: 0x00000001 (已释放)

# 检查 DSP PLL 锁定状态
x/1wx 0x4000a024
# 期望值: bit0 = 1 (已锁定)

# 手动发送握手消息 (CM4→DSP)
set {int}0x40019000 = 0x5A5A5A5A

# 读取 DSP 回复 (DSP→CM4, 读 CM4 侧 RDDATA)
x/1wx 0x40019008
```

---

## 7. 扩展建议

### 7.1 添加超时重试机制到 DSP 端

```c
static int dsp_wait_handshake_with_timeout(uint32_t timeout_loops)
{
    uint32_t loops = 0;
    
    while (loops < timeout_loops) {
        if (dsp_mailbox_has_data()) {
            uint32_t msg = dsp_mailbox_read();
            if (msg == HANDSHAKE_MSG_INIT) {
                dsp_mailbox_write(HANDSHAKE_MSG_ACK);
                return 0;
            }
        }
        loops++;
        for (volatile int i = 0; i < 100; i++);
    }
    
    return -1;  /* 超时 */
}
```

### 7.2 添加心跳丢失检测

```c
/* CM4 端 */
static uint32_t g_last_reply_seq = 0;
static uint32_t g_lost_heartbeat_count = 0;

void check_heartbeat_reply(uint8_t expected_seq, uint8_t received_seq)
{
    if (received_seq != expected_seq) {
        g_lost_heartbeat_count++;
        printf("[CM4] Warning: heartbeat seq mismatch (expected=%u, got=%u)\r\n",
               expected_seq, received_seq);
    }
}
```

---

## 8. 总结

本文档提供了 CM4-DSP 握手与心跳通信的完整实现方案：

1. **启动时序**：GDB 负责 DSP 域复位/解复位，CM4 负责 PLL、UART3 和 warm reset
2. **握手流程**：CM4 周期发送 INIT，DSP 采用幂等 ACK 策略确保最终收敛
3. **心跳机制**：CM4 由 SysTick 触发打印，DSP 由 Mailbox 消息触发打印
4. **协议定义**：共享头文件确保双方格式一致
5. **DSP 适配重点**：优先完成 mailbox 初始化、幂等握手、正确 heartbeat 回复格式

DSP 端开发人员可优先参考第 5.4 节的“需要完善和迭代适配的内容”执行改造，再结合第 5.3 节模板实现落地。
