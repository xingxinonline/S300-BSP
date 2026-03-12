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
5. 如果需要 CM4 让 MM 配置进入运行态，DSP 发送统一的 MM 运行态使能通知给 CM4，而不是直接写主板寄存器。
6. 收到 `SYS.START_STREAM` 后回复 `ACK(kind=SYS, code=START_STREAM)` 并进入 `RUNNING`。
7. `RUNNING` 状态下持续推理，并在每帧结束后发送结果通知。

和 CM4 侧的对应关系建议保持严格对齐：

| DSP 状态 | 对应的 CM4 观察点 | DSP 约束 |
| --- | --- | --- |
| `WAIT_HELLO` | CM4 处于 `HANDSHAKING` 并周期重发 `SYS.HELLO` | 不要做耗时初始化阻塞 `HELLO_ACK` |
| `READY` | CM4 已收到 `HELLO_ACK`，即将发送 `SYS.CM4_RESOURCE_READY` | 接下来只接受当前 session 的资源声明 |
| `RESOURCE_ACCEPTED` | CM4 正等待 `DSP_MODEL_READY` 和配置链路推进 | 发送 `DSP_MODEL_READY` 后继续处理 `CONFIG_APPLY`/`BUFFER_BIND` |
| `CONFIGURED` | CM4 已发 `SYS.START_STREAM` | 准备推理资源，但不要提前发数据面结果 |
| `RUNNING` | CM4 已收到 `ACK(START_STREAM)` 并开始 heartbeat | 才开始发送 `MAILBOX_MSG_TYPE_MULTI` 或 `NO_RESULT` |
| `ERROR` | CM4 可能在 2 秒内放弃当前 session 并回到 `RESET` | 不要再对旧 session 发 ACK/状态 |

DSP 侧最容易踩坑的点有两个：

1. 不要在回 `HELLO_ACK` 之前做模型加载、共享内存清零、外设重建这类耗时操作。
2. 一旦发现 CM4 的 session 已经变化，应立刻放弃旧状态并重新等待新的 `SYS.HELLO`。

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

运行时同步通知：

1. `FD_RT_MAKE_MM_ENABLE_REQ()`

DSP 只负责声明“请使能 MM 运行态”。CM4 收到后根据当前板型决定是只写 `0x70`，还是同时写 `0x70` 和 `0x1E0`。这条通知应在 DSP 收到 `START_STREAM` 的 ACK 并进入运行态后再发送。

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
static bool g_mm_enable_posted;
static DetectionResult_t g_result;

for (;;) {
    if (mailbox_has_data()) {
        uint32_t msg = mailbox_read();

        if (is_hello(msg)) {
            g_session_id = get_session(msg);
            reset_runtime_state();
            g_mm_enable_posted = false;
            send_hello_ack(g_session_id);
            g_state = READY;
            continue;
        }

        if (!session_matches(msg, g_session_id)) {
            continue;
        }

        handle_control_message(msg);
    }

    if ((g_state == RUNNING) && !g_mm_enable_posted) {
        mailbox_write(FD_RT_MAKE_MM_ENABLE_REQ());
        g_mm_enable_posted = true;
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
