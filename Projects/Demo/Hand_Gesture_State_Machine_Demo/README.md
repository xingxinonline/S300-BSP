# Hand_Gesture_State_Machine_Demo

本 Demo 参考 `Face_Detection_State_Machine_Demo` 的 CM4-DSP 控制面骨架，改成手势检测场景，目标是验证手势检测模型接入时的最小闭环：

1. CM4 与 DSP 完成带 session 的控制面握手。
2. CM4 完成摄像头、MM、LCD 初始化后，通过 `SYS.CM4_RESOURCE_READY` 向 DSP 宣告视频资源就绪。
3. DSP 依次返回 `SYS.DSP_MODEL_READY`、`ACK(CONFIG_APPLY)`、`ACK(BUFFER_BIND)`、`ACK(START_STREAM)`，CM4 才进入运行态。
4. 运行态下 DSP 发送 `MAILBOX_MSG_TYPE_MULTI | offset` 上报手势检测结果，并可按统一运行时通知请求由 CM4 执行 MM 运行态使能。
5. CM4 只负责状态机、显示和协议校验；你后续只需要替换 DSP 侧手势检测实现即可。

这个 Demo 同样刻意不引入跟踪逻辑。CM4 不依赖 `track_id`、`miss_count` 或 Kalman 状态；DSP 只要填充手势框和类型即可。

## 架构分工

1. CM4：初始化 camera/MM/LCD、驱动状态机、校验 session、接收 DSP 的 MM 运行态使能请求、显示手势框并打印 `PALM/PEACE` 分类。
2. DSP：完成控制面响应、做手势检测、把 `DetectionResult_t` 写入共享内存、通过 mailbox 通知结果偏移。
3. 数据面：检测结果结构复用 `Algorithm_Models/protocol/detection_proto.h`。
4. 控制面：状态机消息复用 `Algorithm_Models/protocol/control_proto.h`。

## 状态机说明

CM4 状态机实现在 `Src/hand_gesture_app.c`，与参考的人脸状态机 Demo 保持一致：

1. `RESET`：递增 session、warm reset DSP、重同步 mailbox、发送 `SYS.HELLO`。
2. `HANDSHAKING`：等待 `HELLO_ACK`，200 ms 重试 `SYS.HELLO`。
3. `DSP_READY`：收到 `HELLO_ACK` 后立即发送 `SYS.CM4_RESOURCE_READY`。
4. `CM4_RESOURCE_READY`：等待 `DSP_MODEL_READY`、`ACK(CONFIG_APPLY)`、`ACK(BUFFER_BIND)`。
5. `CONFIGURED`：收到 `ACK(BUFFER_BIND)` 后发送 `SYS.START_STREAM`。
6. `RUNNING`：接收检测结果、处理 MM 运行态通知、每 1000 ms 发送 heartbeat。
7. `ERROR`：收到 `NACK` 或初始化失败，2 秒后整轮会话重启。

## 数据面约定

1. DSP 发送 `MAILBOX_MSG_TYPE_MULTI | offset`，其中 `offset` 指向 `DSP_DETECTION_BASE_ADDR` 下的 `DetectionResult_t`。
2. `DetectionBox_t.type` 应填手势类类型。联调阶段可先用 `DETECTION_TYPE_GESTURE` 跑通链路，但正式接入分类时应改为 `DETECTION_TYPE_PALM` 或 `DETECTION_TYPE_PEACE`，这样 CM4 日志才能明确打印具体手势类别。
3. `score` 建议使用 `[0,1]` 浮点或 `[0,100]` 百分比，CM4 侧会统一归一化。
4. `track_id`、`vx/vy`、`speed`、`kf_confidence`、`miss_count` 可以全部填 0。

## DSP 适配注意事项

这次联调已经验证 CM4 控制面、握手、叠框链路都已跑通；后续适配重点在 DSP 输出内容，而不是再修改 CM4 状态机。

1. 不要继续沿用人脸 demo 的内部语义。DSP 日志、变量名、过滤逻辑都应切到手势场景，避免再出现 `face = ...` 这类残留输出。
2. 不要保留“先产出一批 face 框，再做 reject/filter”的旧流程。手势 demo 期望 DSP 直接输出最终有效手势框。
3. 没有有效手势时，应输出 `count = 0`，或发送 `MAILBOX_MSG_TYPE_NO_RESULT`；不要继续上报无效占位框。
4. `score` 必须是协议约定的可解释值。推荐直接写 `[0,1]` 浮点置信度，避免出现 `9999%`、`2147483647%` 这类无意义日志。
5. 框坐标必须满足 `x1 <= x2`、`y1 <= y2`，并且宽高大于 2 像素；否则会被 CM4 叠框层直接过滤。
6. 如果已经能区分具体手势，请优先输出 `PALM/PEACE`，不要长期停留在泛化的 `GESTURE` 类型，否则 CM4 只能显示通用手势类别。

## DSP 自检清单

DSP 适配完成后，可按下面顺序自检：

1. 日志中不再出现 `face = ...`、人脸 raw/reject/filter 等残留字样。
2. `DetectionResult_t.magic/version/count/frame_id` 填写正确，且 `frame_id` 单调递增。
3. `count > 0` 时，每个框的 `type` 为 `GESTURE/PALM/PEACE` 之一，推荐 `PALM/PEACE`。
4. `score` 为有效浮点置信度，CM4 侧不再打印异常百分比现象。
5. 无目标时返回 `count = 0` 或 `NO_RESULT`，CM4 叠框能正常清空。
6. 切换 `PALM -> PEACE` 时，串口能看到主类型切换日志。

## 当前显示/日志策略

1. 只绘制手势类目标框，不绘制 `FACE/PERSON`。
2. 每隔固定帧打印一次 `frame=... gestures=... primary=...`。
3. 当主类型从 `UNKNOWN` 变为 `PALM/PEACE/GESTURE` 时，额外打印一次分类切换日志。

## 目录说明

1. `Src/main.c`：板级启动、SysTick 和 app tick。
2. `Src/hand_gesture_app.c`：CM4 控制面状态机、视频链路初始化。
3. `Src/hand_gesture_overlay.c`：手势检测叠框，只处理手势类结果。
4. `algorithm_reference/README.md`：DSP 最小实现指引。
5. `model_bin/README.md`：DSP 固件放置约定。

## 构建目标

1. `s300_hand_gesture_state_machine_demo`
2. `dbg_hand_gesture_state_machine_demo`
3. `img_s300_hand_gesture_state_machine_demo`

## 建议联调顺序

1. 先跑通 `HELLO -> HELLO_ACK`。
2. 再跑通 `CM4_RESOURCE_READY -> DSP_MODEL_READY -> ACK(CONFIG_APPLY) -> ACK(BUFFER_BIND) -> ACK(START_STREAM)`。
3. 然后让 DSP 固定输出一个 `PALM` 或 `PEACE` 静态框，验证叠框方向和分类日志。
4. 静态框正确后，再接入真实手势检测推理。