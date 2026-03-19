# Human_Tracking_State_Machine_Demo

本 Demo 基于 Human_Detection_State_Machine_Demo 平行复制，用来承载“人形检测 + DSP 跟踪语义”的后续接入工作。

当前这一版先保持与人形检测状态机 Demo 相同的控制面骨架，目标是先把独立工程、构建目标、GDB 脚本和 model_bin 目录拆出来，避免后续追踪改造污染纯检测基线。

## 当前阶段目标

1. 保持 CM4-DSP session 状态机握手闭环。
2. 保持 CM4 负责 camera/MM/LCD 初始化与 MM 运行态同步。
3. 为后续 DSP 输出 track_id、selected_idx、tracker_state 等追踪语义预留独立工程入口。

## 当前状态

1. 当前代码骨架仍复用 Human_Detection_State_Machine_Demo 的 session 状态机，但已补上 TRACK_START、TRACK_STOP、TRACK_RESET 控制命令。
2. 当前 overlay 已按 tracking 语义消费 DetectionResult_t：主目标优先使用 selected_idx，高亮显示并透传 track_id、tracker_state、tracker_flags；日志中的 raw_tracker/raw_flags 仅表示 DSP 原始字段。未追踪时目标框为灰色，追踪确认后主目标框为绿色。
3. CM4 侧增加了最小 tracking consumer，可读取 DSP 当前唯一主目标状态，并通过串口手动触发 TRACK_START、TRACK_STOP、TRACK_RESET；运行日志会同时打印 mode 和 effective，用于区分本地控制状态与 DSP 原始 tracker 字段。
4. 后续真正的 tracking 适配继续只在本 Demo 内完成，不回灌到 Human_Detection_State_Machine_Demo。

## 目录说明

1. Src/main.c：板级启动、SysTick 和 app tick。
2. Src/human_tracking_app.c：CM4 控制面状态机、视频链路初始化。
3. Src/human_tracking_overlay.c：消费 tracking 结果，高亮主目标，并暴露当前 DSP 主目标状态给 app。
4. algorithm_reference/README.md：DSP 侧 tracking 版本最小实现约束。
5. model_bin/README.md：DSP 固件放置约定。

## 构建目标

1. s300_human_tracking_state_machine_demo
2. dbg_human_tracking_state_machine_demo
3. img_s300_human_tracking_state_machine_demo

## 后续适配建议

1. DSP 填写 track_id、selected_idx、tracker_state、tracker_flags。
2. CM4 通过调试串口接收单字符命令：s=start、x=stop、r=reset、p=status。
3. CM4 默认不自动发起 tracking，仅在串口收到 start 命令后发送 TRACK_START。
4. 该 Demo 仍不直接接完整产品级云台控制，重点先验证 CM4-DSP tracking 控制闭环。
