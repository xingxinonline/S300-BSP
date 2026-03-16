# XM04 BLE HID Demo

这个 Demo 用 gimbal_master 板上的 UART1 直接驱动 XM04 蓝牙模块，当前默认工作在纯 HID 测试路径，先验证 HID 发包链路，再决定是否启用 AT 配置路径。

## 硬件约定

- 板型: gimbal_master
- XM04 UART: UART1, GPIO16(RX) / GPIO17(TX), 默认 9600 波特率
- XM04 AT/PIO11: 默认不启用；只有在显式打开 AT 模式时才需要
- 调试串口: BOARD_DEBUG_UART

## 调试方法

1. 先把 XM04 串口接到主板 UART1。
2. 配置 gimbal_master 后，编译目标 `s300_xm04_ble_hid_demo`。
3. 打开调试串口日志，按启动日志里的命令键直接触发 HID 发包。

## 命令

- `p`: 播放/暂停
- `]`: 下一曲
- `[`: 上一曲
- `+`: 音量加
- `-`: 音量减
- `1`: Android Home
- `2`: Power
- `3`: iOS 软键盘切换
- `k`: 发送键盘 `A`
- `r`: 释放所有 HID 键
- `d`: 导出 UART1 RX 缓冲

启用 AT 模式后才可用:

- `a`: 发送基础 `AT`
- `v`: 查询版本
- `s`: 查询连接状态
- `g`: 查询蓝牙名
- `n`: 设置蓝牙名为 `S300-XM04`
- `m`: 设置 HID 模式
- `u`: 查询当前模式
- `t`: 查询认证模式
- `x`: 复位模块

## 说明

- Demo 保持 SDK 的原始 HID 报文格式: `[len, 0x00, 0xA1, report_id, payload...]`。
- Consumer Control 现已按 XM04 手册实现为 4 字节位图语义，而不是标准 HID usage ID 语义。
- 例如音量加应发送 `08 00 A1 03 00 04 00 00`，音量减应发送 `08 00 A1 03 00 02 00 00`。
- 手册示例中的 Android Home / Power / iOS 软键盘切换也作为独立测试命令保留。
- Demo 默认跟随 SDK 使用 9600 波特率；如果你的 XM04 已改过串口速率，可以在 CMake 配置时覆盖 `XM04_UART_BAUDRATE`。
- Demo 默认关闭 AT 模式，避免在 HID 测试阶段依赖未知的 AT/PIO11 控制脚。
- 如果后续要打开 AT 模式，可在 CMake 配置时设置 `XM04_ENABLE_AT_MODE=1`，并按需要覆盖 `XM04_AT_PIN`。
