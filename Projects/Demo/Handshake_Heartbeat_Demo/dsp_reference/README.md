# DSP 端参考实现

本目录包含 DSP 端握手和 Heartbeat 处理的参考实现代码。

## 文件说明

| 文件                   | 说明               |
| ---------------------- | ------------------ |
| `dsp_handshake_main.c` | DSP 主程序参考实现 |
| `dsp_mailbox_hal.h`    | Mailbox 硬件抽象层 |

## 使用方法

### 1. 复制文件到 DSP 工程

将以下文件复制到 DSP 工程中：

```
Algorithm_Models/protocol/handshake_proto.h  → DSP 工程的 include 目录
dsp_reference/dsp_mailbox_hal.h              → DSP 工程的 include 目录
dsp_reference/dsp_handshake_main.c           → DSP 工程的 src 目录（或参考集成）
```

### 2. 适配 Mailbox 驱动

参考 `dsp_handshake_main.c` 中的底层函数，根据实际 DSP 工程环境适配：

- `dsp_mailbox_init()` - 初始化 Mailbox
- `dsp_mailbox_rx_empty()` - 检查接收 FIFO 是否为空
- `dsp_mailbox_read()` - 读取数据
- `dsp_mailbox_write()` - 写入数据

### 3. 地址配置

当前实测可工作的 Mailbox 映射如下：

```c
/* CM4 视角 */
#define MAILBOX_BASE            0x40019000u

/* DSP 视角 */
#define DSP_MAILBOX_BASE        0x44080400u
```

说明：

- 当前使用的是同一个 Mailbox 外设的双 FIFO 视图，不再是 `TX_BASE` / `RX_BASE` 两个独立基址
- DSP 向 CM4 发送时，写 `DSP_MAILBOX_BASE + 0x00` (`WRDATA`)
- DSP 从 CM4 接收时，读 `DSP_MAILBOX_BASE + 0x08` (`RDDATA`)
- CM4 侧实测接收 DSP `HANDSHAKE_ACK` 和 `HEARTBEAT_REPLY` 的基址是 `0x40019000`

### 4. 集成到 DSP 工程

将握手和 Heartbeat 处理逻辑集成到 DSP 工程的主循环中。

## 通信流程

```
┌─────────────────────────────────────────────────────────────────┐
│                        握手阶段                                  │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   CM4                                          DSP              │
│    │                                            │               │
│    │  1. 初始化外设、启动 DSP                   │               │
│    │──────────────────────────────────────────>│               │
│    │                                            │               │
│    │  2. 初始化 Mailbox                         │               │
│    │                                            │<──────────────│
│    │                                            │               │
│    │  3. HANDSHAKE_INIT (0x5A5A5A5A)           │               │
│    │──────────────┬─────────────────────────> wait             │
│    │              │                             │               │
│    │  4. HANDSHAKE_ACK (0xA5A5A5A5)            │               │
│    │<─────────────┴─────────────────────────────│               │
│    │                                            │               │
│    │  握手完成，双方进入工作状态                 │               │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                        Heartbeat 阶段                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   CM4                                          DSP              │
│    │                                            │               │
│    │  [SysTick 1s] 打印 CM4 Heartbeat           │               │
│    │                                            │               │
│    │  [定时 2s] HEARTBEAT_TRIGGER               │               │
│    │─────────────────────────────────────────>│               │
│    │                                            │               │
│    │                                      打印 DSP Heartbeat    │
│    │                                            │               │
│    │  HEARTBEAT_REPLY                           │               │
│    │<─────────────────────────────────────────│               │
│    │                                            │               │
│    │  ... 循环 ...                              │               │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## 消息格式

| 消息              | 方向      | 值           | 说明           |
| ----------------- | --------- | ------------ | -------------- |
| HANDSHAKE_INIT    | CM4 → DSP | `0x5A5A5A5A` | 握手初始化     |
| HANDSHAKE_ACK     | DSP → CM4 | `0xA5A5A5A5` | 握手确认       |
| HEARTBEAT_TRIGGER | CM4 → DSP | `0xBEA7xxxx` | Heartbeat 触发 |
| HEARTBEAT_REPLY   | DSP → CM4 | `0xACE0xxyy` | Heartbeat 回复 |

## 完整设计文档

详细的设计说明、启动时序、协议定义请参考：

- [CM4_DSP_Handshake_Design.md](../../../../docs/CM4_DSP_Handshake_Design.md) - 完整设计文档

## 注意事项

1. CM4 作为主控核，负责 DSP 的启动，所以 DSP 程序应该在 CM4 启动前预先加载到 DSP 内存中
2. 如果握手超时，优先检查 DSP 是否使用 `0x44080400` 这一实测有效的 DSP 视角 Mailbox 基址
3. 实际项目中可能需要添加更完善的错误处理和超时机制
