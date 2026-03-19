# DSP 侧实现指引：Human_Detection_State_Machine_Demo

本文档说明 DSP 侧如何实现与 s300_human_detection_state_machine_demo 对应的最小版本。

目标很明确：

1. 控制面走新的状态机握手。
2. DSP 只做人形检测。
3. DSP 只发送多目标坐标，不做追踪，不做目标选择，不做 Kalman coast。

## 1. DSP 侧最小状态机

建议 DSP 状态如下：

1. WAIT_HELLO
2. READY
3. RESOURCE_ACCEPTED
4. CONFIGURED
5. RUNNING
6. ERROR

推荐状态迁移：

1. 收到 SYS.HELLO(session) 后，锁定 session_id，回复 SYS.HELLO_ACK。
2. 收到 SYS.CM4_RESOURCE_READY(session, VIDEO, ...) 后，初始化模型和结果缓冲，回复 SYS.DSP_MODEL_READY。
3. 收到 CMD.CONFIG_APPLY 后回复 ACK(kind=CMD, code=CONFIG_APPLY)。
4. 收到 CMD.BUFFER_BIND 后回复 ACK(kind=CMD, code=BUFFER_BIND)。
5. 如果需要 CM4 让 MM 配置进入运行态，DSP 发送统一的 MM 运行态使能通知给 CM4，而不是直接写主板寄存器。
6. 收到 SYS.START_STREAM 后回复 ACK(kind=SYS, code=START_STREAM) 并进入 RUNNING。
7. RUNNING 状态下持续推理，并在每帧结束后发送结果通知。

## 2. DetectionResult_t 的最小填写要求

DSP 每次输出结果时至少填写：

1. magic = DETECTION_RESULT_MAGIC
2. version = DETECTION_PROTOCOL_VERSION
3. frame_id：逐帧递增
4. count：本帧检测到的人体数
5. selected_idx = -1
6. tracker_state = TRACKER_STATE_DISABLED 或 TRACKER_STATE_IDLE
7. tracker_flags = 0

每个 DetectionBox_t 最少填写：

1. score
2. x1, y1, x2, y2
3. type = DETECTION_TYPE_PERSON

## 3. 推荐伪代码

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
        mailbox_write(HD_RT_MAKE_MM_ENABLE_REQ());
        g_mm_enable_posted = true;
    }

    if (g_state == RUNNING) {
        int human_count = run_human_detection(&g_result);
        if (human_count > 0) {
            mailbox_write(MAILBOX_MAKE_MSG(MAILBOX_MSG_TYPE_MULTI, result_offset));
        } else {
            mailbox_write(MAILBOX_MAKE_MSG(MAILBOX_MSG_TYPE_NO_RESULT, 0));
        }
    }
}
```
