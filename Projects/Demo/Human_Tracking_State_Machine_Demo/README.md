# Human_Tracking_State_Machine_Demo

本 Demo 基于 Human_Detection_State_Machine_Demo 平行复制，用来承载“人形检测 + DSP 跟踪语义”的后续接入工作。

当前这一版先保持与人形检测状态机 Demo 相同的控制面骨架，目标是先把独立工程、构建目标、GDB 脚本和 model_bin 目录拆出来，避免后续追踪改造污染纯检测基线。

## 当前阶段目标

1. 保持 CM4-DSP session 状态机握手闭环。
2. 保持 CM4 负责 camera/MM/LCD 初始化与 MM 运行态同步。
3. 为后续 DSP 输出 track_id、selected_idx、tracker_state 等追踪语义预留独立工程入口。

## 当前状态

1. 当前代码骨架与 Human_Detection_State_Machine_Demo 等价，只完成了工程身份和日志前缀切换。
2. 当前 overlay 仍按“人形框显示”消费 DetectionResult_t。
3. selected_idx 目前只用于日志透传，尚未做主目标高亮或 track_id 呈现。
4. 后续真正的 tracking 适配应在本 Demo 内完成，不回灌到 Human_Detection_State_Machine_Demo。

## 目录说明

1. Src/main.c：板级启动、SysTick 和 app tick。
2. Src/human_tracking_app.c：CM4 控制面状态机、视频链路初始化。
3. Src/human_tracking_overlay.c：当前先复用人形多目标叠框，后续在这里接 selected_idx / track_id 呈现。
4. algorithm_reference/README.md：DSP 侧 tracking 版本最小实现约束。
5. model_bin/README.md：DSP 固件放置约定。

## 构建目标

1. s300_human_tracking_state_machine_demo
2. dbg_human_tracking_state_machine_demo
3. img_s300_human_tracking_state_machine_demo

## 后续适配建议

1. DSP 填写 track_id、selected_idx、tracker_state、tracker_flags。
2. CM4 overlay 根据 selected_idx 区分主目标与普通目标。
3. 再决定是否在本 Demo 内增加简单的 tracking consumer，而不是直接接完整产品级云台控制。
