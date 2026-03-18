# Face_Recognition_State_Machine_Demo

本 Demo 复用新的 CM4-DSP 控制面状态机骨架，适配 DSP 已升级为“人脸检测 + 人脸识别特征输出”的场景。

当前第一版行为刻意保持简单：

1. CM4 先按 session 化控制面把 DSP 推到 RUNNING。
2. DSP 运行态回传检测结果；当主脸 embedding 有效时，把它附着在同一帧 DetectionResult_t 尾部。
3. CM4 在第一次收到有效 embedding 时，把 selected_idx 对应的人脸特征保存为 anchor。
4. 后续每帧最多消费 1 个 embedding，并与该锚点做相似度对比。

## 结果消费策略

当前版本不做注册库管理，也不做多脸跟踪绑定，只验证最小识别闭环：

1. `selected_idx` 指向当前帧被 DSP 选中的主脸框。
2. `feature_flags & DETECTION_RESULT_FLAG_HAS_FEATURE` 为真时，`feature_vector[128]` 才有效。
3. Demo 第一次收到有效特征时，会把该向量保存为 anchor。
4. 之后每帧最多使用 1 个 embedding 与 anchor 做点积相似度计算。
5. 如果没有有效特征，仍然只显示人脸框，不丢弃检测结果。

相似度公式当前复用旧 Face_Recognition_Demo 的实现：

1. 逐维 int8 点积。
2. 再按 `16129` 归一到 0~100。

## 协议边界

1. 控制面继续复用 `Algorithm_Models/protocol/control_proto.h`。
2. 本 Demo 的数据面结果结构使用本地 `Inc/detection_protocol.h`，与 DSP 新版 `DetectionResult_t` 对齐。
3. 当前协议版本为 `0x0203`，在检测框尾部追加 `feature_flags`、`feature_dim` 和单个 `feature_vector[128]`。
4. CM4 兼容旧 `0x0202` 检测-only 结果；旧版本下只画框，不做特征比对。

## 构建目标

1. `s300_face_recognition_state_machine_demo`
2. `dbg_face_recognition_state_machine_demo`
3. `img_s300_face_recognition_state_machine_demo`

## 构建

1. `cmake -B build -G Ninja`
2. `ninja -C build s300_face_recognition_state_machine_demo img_s300_face_recognition_state_machine_demo`

## 目录说明

1. `Src/face_recognition_app.c`：CM4 控制面状态机、视频链路初始化。
2. `Src/face_recognition_overlay.c`：检测框显示 + anchor 特征比对。
3. `Inc/detection_protocol.h`：本 Demo 使用的检测+识别数据面协议头。
4. `model_bin/`：可选本地 DSP bin 覆盖目录；若为空则回退到 `Algorithm_Models/Face_Recognition`。bin 后投放也可以，后续构建/调试会自动重新选择本地目录，无需手动删 `build` 或强制 reconfigure。
