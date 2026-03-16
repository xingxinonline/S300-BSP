# DSP 侧实现指引：Hand_Gesture_State_Machine_Demo

本文档说明 DSP 侧如何实现与 `s300_hand_gesture_state_machine_demo` 对应的最小版本。

目标很明确：

1. 控制面走新的状态机握手。
2. DSP 只做手势检测。
3. DSP 只发送手势框和手势类型，不做追踪，不做目标选择，不做 Kalman coast。

结合当前 CM4 联调结果，后续 DSP 适配时请把重点放在“结果内容对齐”而不是控制面上。当前 CM4 demo 已验证可正常完成握手、配置、启动流和接收结果通知。

## 1. DSP 侧最小状态机

建议 DSP 状态如下：

1. `WAIT_HELLO`
2. `READY`
3. `RESOURCE_ACCEPTED`
4. `CONFIGURED`
5. `RUNNING`
6. `ERROR`

推荐状态迁移与 `Face_Detection_State_Machine_Demo` 保持一致：

1. 收到 `SYS.HELLO(session)` 后，锁定 `session_id`，回复 `SYS.HELLO_ACK`。
2. 收到 `SYS.CM4_RESOURCE_READY(session, VIDEO, ...)` 后，初始化模型和结果缓冲，回复 `SYS.DSP_MODEL_READY`。
3. 收到 `CMD.CONFIG_APPLY` 后回复 `ACK(kind=CMD, code=CONFIG_APPLY)`。
4. 收到 `CMD.BUFFER_BIND` 后回复 `ACK(kind=CMD, code=BUFFER_BIND)`。
5. 如果需要 CM4 让 MM 配置进入运行态，DSP 发送统一的 MM 运行态使能通知给 CM4。
6. 收到 `SYS.START_STREAM` 后回复 `ACK(kind=SYS, code=START_STREAM)` 并进入 `RUNNING`。
7. `RUNNING` 状态下持续推理，并在每帧结束后发送结果通知。

## 2. DetectionResult_t 的最小填写要求

共享头文件：

1. `Algorithm_Models/protocol/detection_proto.h`

DSP 每次输出结果时至少填写：

1. `magic = DETECTION_RESULT_MAGIC`
2. `version = DETECTION_PROTOCOL_VERSION`
3. `frame_id`：逐帧递增
4. `count`：本帧检测到的手势数
5. `selected_idx = -1`
6. `tracker_state = TRACKER_STATE_DISABLED` 或 `TRACKER_STATE_IDLE`
7. `tracker_flags = 0`

每个 `DetectionBox_t` 最少填写：

1. `score`
2. `x1, y1, x2, y2`
3. `type = DETECTION_TYPE_PALM` 或 `DETECTION_TYPE_PEACE`

额外约束：

1. 推荐 `score` 直接填写 `[0,1]` 浮点值，例如 `0.87f`。
2. 不要把内部整数、定点值或 debug 哨兵值直接写进 `score`，否则 CM4 侧会看到 `9999%`、`2147483647%` 这类异常日志。
3. `x1 <= x2`、`y1 <= y2`，并确保框宽高大于 2 像素。
4. 如果当前模型只能先输出“有手势”而没有细分类，临时可用 `DETECTION_TYPE_GESTURE` 跑通，但真正交付前应切换为 `PALM/PEACE`。

可以全部置零的字段：

1. `track_id = 0`
2. `vx = 0`
3. `vy = 0`
4. `speed = 0`
5. `kf_confidence = 0`
6. `miss_count = 0`
7. `lm[10] = 0`

明确禁止继续沿用人脸 demo 语义：

1. 不要再输出 `face = ...` 这类日志。
2. 不要保留 face demo 的“raw -> reject -> filter”产物作为最终上报结果。
3. 不要把无效占位框也算进 `count`；`count` 应表示最终有效手势框数量。

## 3. 结果通知方式

推荐固定一块结果缓冲，例如：

1. 在 DSP `sram1` 里放一个 `DetectionResult_t g_result`
2. 每帧覆盖写 `g_result`
3. 计算 `offset = (uintptr_t)&g_result - DSP_DETECTION_BASE_ADDR_DSP_VIEW`
4. 发送 `MAILBOX_MAKE_MSG(MAILBOX_MSG_TYPE_MULTI, offset)`

CM4 会按 `DSP_DETECTION_BASE_ADDR + offset` 取到结果。

如果当前帧没有有效手势结果，建议：

1. 直接把 `g_result.count` 置 0 后继续发送 `MAILBOX_MSG_TYPE_MULTI`；或
2. 发送 `MAILBOX_MSG_TYPE_NO_RESULT`

两种方式都可以，但不要继续发送带无效框坐标的假结果。

## 4. 调试建议

第一阶段建议不要一上来接真实模型，而是先让 DSP 输出固定框：

1. `count = 1`，中央输出一个 `PALM` 框。
2. 验证 CM4 叠框方向、坐标系和镜像关系。
3. 再切换到 `PEACE`，验证分类日志切换。
4. 多框显示正确后，再接入真实手势检测模型。

推荐把联调拆成三步：

1. 先固定输出 `PALM`，确认 `[HG-OVL] primary gesture -> PALM`。
2. 再固定输出 `PEACE`，确认日志能切换到 `PEACE`。
3. 最后再接真实模型，并检查 `count/type/score` 是否仍满足上述约束。