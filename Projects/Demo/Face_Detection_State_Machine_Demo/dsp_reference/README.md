# DSP 侧实现指引：Face_Detection_State_Machine_Demo

本文档说明 DSP 侧如何实现与 `s300_face_detection_state_machine_demo` 对应的最小版本。

目标很明确：

1. 控制面走新的状态机握手。
2. DSP 只做人脸检测。
3. DSP 只发送多目标坐标，不做追踪，不做目标选择，不做 Kalman coast。

## 1. DSP 侧最小状态机

建议 DSP 状态如下：

1. `WAIT_HELLO`
2. `READY`
3. `RESOURCE_ACCEPTED`
4. `CONFIGURED`
5. `RUNNING`
6. `ERROR`

推荐状态迁移：

1. 收到 `SYS.HELLO(session)` 后，锁定 `session_id`，回复 `SYS.HELLO_ACK`。
2. 收到 `SYS.CM4_RESOURCE_READY(session, VIDEO, ...)` 后，初始化模型和结果缓冲，回复 `SYS.DSP_MODEL_READY`。
3. 收到 `CMD.CONFIG_APPLY` 后回复 `ACK(kind=CMD, code=CONFIG_APPLY)`。
4. 收到 `CMD.BUFFER_BIND` 后回复 `ACK(kind=CMD, code=BUFFER_BIND)`。
5. 收到 `SYS.START_STREAM` 后回复 `ACK(kind=SYS, code=START_STREAM)` 并进入 `RUNNING`。
6. `RUNNING` 状态下持续推理，并在每帧结束后发送结果通知。

## 2. DSP 必须支持的 mailbox 消息

必收：

1. `SYS.HELLO`
2. `SYS.CM4_RESOURCE_READY`
3. `CMD.CONFIG_APPLY`
4. `CMD.BUFFER_BIND`
5. `SYS.START_STREAM`
6. `SYS.HEARTBEAT`

必回：

1. `SYS.HELLO_ACK`
2. `SYS.DSP_MODEL_READY`
3. `ACK(kind=CMD, code=CONFIG_APPLY)`
4. `ACK(kind=CMD, code=BUFFER_BIND)`
5. `ACK(kind=SYS, code=START_STREAM)`
6. `SYS.HEARTBEAT` 或 `STATUS`

数据面通知：

1. `MAILBOX_MSG_TYPE_MULTI | offset`
2. 可选 `MAILBOX_MSG_TYPE_NO_RESULT`

## 3. DetectionResult_t 的最小填写要求

共享头文件：

1. `Algorithm_Models/protocol/detection_proto.h`

DSP 每次输出结果时至少填写：

1. `magic = DETECTION_RESULT_MAGIC`
2. `version = DETECTION_PROTOCOL_VERSION`
3. `frame_id`：逐帧递增
4. `count`：本帧检测到的人脸数
5. `selected_idx = -1`
6. `tracker_state = TRACKER_STATE_DISABLED` 或 `TRACKER_STATE_IDLE`
7. `tracker_flags = 0`

每个 `DetectionBox_t` 最少填写：

1. `score`
2. `x1, y1, x2, y2`
3. `type = DETECTION_TYPE_FACE`

可以全部置零的字段：

1. `track_id = 0`
2. `vx = 0`
3. `vy = 0`
4. `speed = 0`
5. `kf_confidence = 0`
6. `miss_count = 0`
7. `lm[10] = 0`

换句话说，只要框坐标和分数是对的，CM4 就能正常显示。

## 4. 结果通知方式

推荐固定一块结果缓冲，例如：

1. 在 DSP `sram1` 里放一个 `DetectionResult_t g_result`
2. 每帧覆盖写 `g_result`
3. 计算 `offset = (uintptr_t)&g_result - DSP_DETECTION_BASE_ADDR_DSP_VIEW`
4. 发送 `MAILBOX_MAKE_MSG(MAILBOX_MSG_TYPE_MULTI, offset)`

CM4 会按 `DSP_DETECTION_BASE_ADDR + offset` 取到结果。

## 5. 推荐伪代码

```c
static uint8_t g_session_id;
static DspState g_state = WAIT_HELLO;
static DetectionResult_t g_result;

for (;;) {
    if (mailbox_has_data()) {
        uint32_t msg = mailbox_read();

        if (is_hello(msg)) {
            g_session_id = get_session(msg);
            reset_runtime_state();
            send_hello_ack(g_session_id);
            g_state = READY;
            continue;
        }

        if (!session_matches(msg, g_session_id)) {
            continue;
        }

        handle_control_message(msg);
    }

    if (g_state == RUNNING) {
        int face_count = run_face_detection(&g_result);
        if (face_count > 0) {
            mailbox_write(MAILBOX_MAKE_MSG(MAILBOX_MSG_TYPE_MULTI, result_offset));
        } else {
            mailbox_write(MAILBOX_MAKE_MSG(MAILBOX_MSG_TYPE_NO_RESULT, 0));
        }
    }
}
```

## 6. 调试建议

第一阶段建议不要一上来接真实模型，而是先让 DSP 输出固定框：

1. `count = 1`，框固定在屏幕中央。
2. 验证 CM4 叠框方向、坐标系和镜像关系。
3. 再切换到 `count = 2/3` 的静态多框。
4. 多框显示正常后，再接真实检测模型。

这样可以把“控制面问题”和“检测模型问题”分开。

## 7. 预期日志

CM4 侧：

1. `RX HELLO_ACK`
2. `RX DSP_MODEL_READY`
3. `RX ACK kind=1 code=0x01 status=0`
4. `STATE CONFIGURED -> RUNNING`
5. `FD-OVL frame=... faces=...`

DSP 侧：

1. `HELLO received`
2. `HELLO_ACK sent`
3. `DSP_MODEL_READY sent`
4. `CONFIG_APPLY acked`
5. `BUFFER_BIND acked`
6. `START_STREAM acked`
7. `frame=N count=M sent`