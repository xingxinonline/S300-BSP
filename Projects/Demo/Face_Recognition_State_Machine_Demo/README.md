# Face_Recognition_State_Machine_Demo

本 Demo 复用新的 CM4-DSP 控制面状态机骨架，适配 DSP v2.4 的“人脸检测 + 人脸识别会话摘要”场景。

当前行为：

1. CM4 先按 session 化控制面把 DSP 推到 RUNNING。
2. DSP 在 RUNNING 后收到 `CMD.FR_START_SESSION`，进入人脸识别会话。
3. DSP 通过 `DetectionResult_t` 同时回传检测框、可选 feature tail，以及 `verify_state` / `verify_score` / `template_count` / `candidate_flags` 摘要。
4. CM4 不再本地维护 anchor 比对逻辑，而是直接消费 DSP 的识别摘要并做 overlay / 日志显示。

## 结果消费策略

当前版本不在 CM4 侧复算识别结论，只做协议消费和状态可视化：

1. `selected_idx` 指向当前帧被 DSP 选中的主脸框。
2. `feature_flags & DETECTION_RESULT_FLAG_HAS_FEATURE` 为真，且 `candidate_flags` 包含 `FEATURE_VALID` 时，`feature_vector[128]` 才有效。
3. `verify_state=WAIT_ANCHOR` 表示会话已启动，但模板库仍为空，DSP 正在等待可注册的人脸。
4. `candidate_flags` 用于解释当前帧为什么没出特征，例如是否仅检测到候选脸、是否允许提特征、是否触发模板入库或融合。
5. `count == 0` 也可能仍然携带有效识别摘要，CM4 不应把它误判为协议异常。

## 协议边界

1. 控制面继续复用 `Algorithm_Models/protocol/control_proto.h`。
2. 本 Demo 的数据面结果结构使用本地 `Inc/detection_protocol.h`，与 DSP 新版 `DetectionResult_t` 对齐。
3. 当前协议版本为 `0x0204`，尾部包含 `feature_flags`、`feature_dim`、`feature_vector[128]` 以及识别摘要字段。
4. `feature_dim` 的实际类型为 `uint32_t`。
5. CM4 兼容旧 `0x0202/0x0203` 检测结果；旧版本下只画框，不消费 v2.4 摘要。

## 运行态判读

1. `RX ACK ... CMD.FR_START_SESSION` 表示会话已启动。
2. 长时间停留 `WAIT_ANCHOR` 且 `cand_flags=0x01(PRESENT)`，表示 DSP 看到了候选脸，但质量门禁未放行提特征；这不是协议错误。
3. 只有当 `cand_flags` 出现 `ALLOW_EXTRACT|FEATURE_VALID`，并经过 DSP 的入库投票后，才会从 `WAIT_ANCHOR` 切到 `VERIFYING`。
4. `template_count > 0` 后，`MATCH / UNCERTAIN / NO_MATCH` 才具备实际识别意义。

## 构建目标

1. `s300_face_recognition_state_machine_demo`
2. `dbg_face_recognition_state_machine_demo`
3. `img_s300_face_recognition_state_machine_demo`

## 构建

1. `cmake -B build -G Ninja`
2. `ninja -C build s300_face_recognition_state_machine_demo img_s300_face_recognition_state_machine_demo`

## 目录说明

1. `Src/face_recognition_app.c`：CM4 控制面状态机、视频链路初始化。
2. `Src/face_recognition_overlay.c`：检测框显示 + DSP v2.4 摘要日志解析。
3. `Inc/detection_protocol.h`：本 Demo 使用的检测+识别数据面协议头。
4. `model_bin/`：可选本地 DSP bin 覆盖目录；若为空则回退到 `Algorithm_Models/Face_Recognition`。bin 后投放也可以，后续构建/调试会自动重新选择本地目录，无需手动删 `build` 或强制 reconfigure。
