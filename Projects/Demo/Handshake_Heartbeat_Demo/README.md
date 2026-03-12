# CM4-DSP Handshake & Heartbeat Demo

本 Demo 演示 S300 芯片上 CM4 核与 DSP 核之间的握手流程和周期性心跳通信。

## 功能说明

### DSP 启动流程（CM4 侧控制）

```
┌─────────────────────────────────────────────────────────────┐
│ 1. dsp_hold_reset()                                         │
│    - DSP_CEVA_RST_CTRL (0x4000a018) = 0                     │
│    - DSP 域复位，复位整个 DSP 时钟域（非自弹起）              │
├─────────────────────────────────────────────────────────────┤
│ 2. CM4 初始化                                               │
│    - SysTick、UART、Mailbox 等                              │
├─────────────────────────────────────────────────────────────┤
│ 3. dsp_clock_init()                                         │
│    - 在域复位状态下配置 DSP PLL (400MHz)                     │
├─────────────────────────────────────────────────────────────┤
│ 4. dsp_start()                                              │
│    - 释放 DSP 域复位 (DSP_CEVA_RST_CTRL = 1)                │
│    - 触发 DSP warm reset (0x4000a010, 自弹起，仅复位内核)    │
│    - DSP 开始执行程序                                        │
└─────────────────────────────────────────────────────────────┘
```

### 握手流程
1. CM4 周期性发送 `HANDSHAKE_INIT` (0x5A5A5A5A)
2. DSP 初始化 Mailbox，等待握手消息
3. DSP 收到后回复 `HANDSHAKE_ACK` (0xA5A5A5A5)
4. CM4 收到 ACK，握手完成，双方进入工作状态

### Heartbeat 机制
- **CM4 Heartbeat**: 由 SysTick 定时器触发（每 1 秒打印一次）
- **DSP Heartbeat**: 由 CM4 通过 Mailbox 发送触发消息（每 2 秒触发一次）

## 目录结构

```
Handshake_Heartbeat_Demo/
├── CMakeLists.txt                  # CM4 工程配置 (含 DSP bin 加载)
├── README.md                       # 本文档
├── gdbinit.handshake.gdb.in        # GDB 脚本模板
├── Src/
│   └── main.c                      # CM4 端主程序
├── model_bin/                      # DSP 二进制文件目录
│   ├── README.md                   # DSP bin 文件说明
│   ├── model_ptcm_boot.bin         # DSP 程序代码 (放置后自动检测)
│   └── model_dtcm_boot.bin         # DSP 数据段 (放置后自动检测)
└── dsp_reference/
    ├── README.md                   # DSP 端使用说明
    ├── dsp_handshake_main.c        # DSP 端参考实现
    └── dsp_mailbox_hal.h           # DSP Mailbox 硬件抽象层
```

## 调试 (带 DSP 加载)

### 1. 放置 DSP bin 文件

将 DSP 编译生成的 bin 文件放到 `model_bin/` 目录：

```
model_bin/
├── model_ptcm_boot.bin    # DSP 程序代码 → 加载到 0x44A00000
└── model_dtcm_boot.bin    # DSP 数据段   → 加载到 0x44800000
```

### 2. 重新配置 CMake

```bash
cmake -B build -G Ninja -DBOARD=gimbal_master
```

配置时会显示检测到的 DSP bin 文件：
```
-- Handshake_Heartbeat_Demo: Checking DSP bin files...
--   DSP bin: .../model_dtcm_boot.bin -> 0x44800000
--   DSP bin: .../model_ptcm_boot.bin -> 0x44A00000
```

### 3. 启动调试

确保 OpenOCD 已连接目标板，然后运行：

```bash
ninja -C build dbg_handshake_hb
```

调试脚本会自动：
1. 让 DSP 保持域复位
2. 加载 DSP bin 到 PTCM/DTCM
3. 加载 CM4 程序到 SRAM
4. 启动 CM4 程序（CM4 会控制 DSP 启动和握手）

## 编译

```bash
# 配置 (选择板子类型)
cmake -B build -G Ninja -DBOARD=gimbal_master

# 编译
ninja -C build s300_handshake_heartbeat
```

## 协议定义

协议头文件位于 `Algorithm_Models/protocol/handshake_proto.h`，定义了：
- 握手消息 (`HANDSHAKE_MSG_INIT`, `HANDSHAKE_MSG_ACK`)
- Heartbeat 触发消息 (`HEARTBEAT_MSG_TRIGGER_MASK`)
- Heartbeat 回复消息 (`HEARTBEAT_MSG_REPLY_MASK`)
- 超时配置参数

## 预期输出

CM4 端串口输出示例：

```
============================================
  S300 CM4-DSP Handshake & Heartbeat Demo
============================================
[CM4] SystemCoreClock = 192000000 Hz
[CM4] Mailbox initialized
[CM4] DSP PLL initialized (400MHz)
[CM4] DSP core started
[CM4] Waiting for DSP handshake...
[CM4] Sent HANDSHAKE_INIT (0x5A5A5A5A)
[CM4] Received from DSP: 0xA5A5A5A5
[CM4] Handshake ACK received! Handshake completed.

[CM4] === Both cores ready, starting heartbeat ===

[CM4] Heartbeat #0 (tick=1100 ms)
[CM4] Heartbeat #1 (tick=2100 ms)
[CM4] Sent HEARTBEAT_TRIGGER seq=0
[CM4] Received DSP HEARTBEAT_REPLY: seq=0, status=0
[CM4] Heartbeat #2 (tick=3100 ms)
...
```

## 注意事项

1. **DSP 程序需先加载**：通过 Flash Boot 或调试器加载 DSP 程序
2. **Mailbox 地址**：CM4 和 DSP 视角的地址不同，请参考 `dsp_reference/README.md`
3. **超时配置**：可通过宏定义调整握手超时时间和重试间隔

## 相关文档

- [mailbox_proto.h](../../../Drivers/SoC/MAILBOX/Include/mailbox_proto.h) - 通用邮箱协议
- [handshake_proto.h](../../../Algorithm_Models/protocol/handshake_proto.h) - 握手协议定义
- [KWS_Mailbox_Protocol_Design.md](../../../docs/KWS_Mailbox_Protocol_Design.md) - 邮箱协议设计参考
