# DSP 侧实现指引：Human_Tracking_State_Machine_Demo

本文档说明 DSP 侧如何实现与 s300_human_tracking_state_machine_demo 对应的最小版本。

当前建议分两阶段推进：

1. 第一阶段：先沿用人形检测状态机 Demo 的控制面握手和结果上报方式，确保独立工程能稳定启动。
2. 第二阶段：再在本 Demo 内引入 DSP 跟踪语义，包括 track_id、selected_idx、tracker_state 和 tracker_flags。

## 第一阶段最低要求

1. 控制面仍走新的状态机握手。
2. DSP 仍做人形检测。
3. 可以先不做真正的 tracking 选择，但输出结构必须与后续 tracking 语义兼容。

## 第二阶段建议要求

1. DetectionBox_t 填写稳定的 track_id。
2. DetectionResult_t 填写 selected_idx，表示当前主跟踪目标。
3. tracker_state / tracker_flags 反映 DSP 当前追踪状态。
4. 如使用 Kalman，vx/vy、speed、kf_confidence 填写为真实有效值。

## 约束

1. 追踪改造只在 Human_Tracking_State_Machine_Demo 内演进。
2. 不要反向污染 Human_Detection_State_Machine_Demo 的纯检测基线。
