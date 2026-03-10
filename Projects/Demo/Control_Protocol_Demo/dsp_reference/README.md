# DSP 侧实现指引：Control_Protocol_Demo

本文档用于指导 DSP 侧实现与 `s300_control_protocol_demo` 对应的联调 Demo。

DSP 编译完成后，请将生成的 `model_ptcm_boot.bin` 和 `model_dtcm_boot.bin` 放入同级目录 `../dsp_bin/`，供 CM4 侧调试脚本和镜像打包使用。

目标不是实现 AI 推理，而是实现一套最小可运行的 DSP 控制面状态机，使 CM4 侧能够完整走通：

1. 建立可信 session
2. 处理 `CM4_RESOURCE_READY`
3. 处理配置与 buffer 绑定
4. 对 `START_STREAM` 返回 ACK
5. 在 `RUNNING` 态处理 `FRAME_READY` / `AUDIO_READY` 的伪通知
6. 周期性返回 `STATUS` 或 `HEARTBEAT`

---

## 1. 必须遵守的约束

1. DSP 可能先于 CM4 上电启动，因此在收到当前轮 `HELLO(session_id)` 之前，DSP 不得发送任何会被 CM4 采信的控制消息。
2. DSP 必须把 `session_id` 锁定为当前轮会话号；后续发回的 `HELLO_ACK`、`ACK`、`NACK`、`STATUS`、`HEARTBEAT` 都必须带同一个 `session_id`。
3. 如果 DSP 检测到 `session_id` 变化，应立即清空内部控制状态，回到新的握手轮次。
4. 本 Demo 不需要做真实视频/音频处理，也不需要做模型推理；只需要记录 `FRAME_READY` / `AUDIO_READY` 通知并打印日志。

---

## 2. DSP 侧最小状态机

建议 DSP 侧状态机与 CM4 保持一致：

1. `RESET`
2. `WAIT_HELLO`
3. `READY`
4. `RESOURCE_ACCEPTED`
5. `CONFIGURED`
6. `RUNNING`
7. `ERROR`

推荐状态迁移：

1. 上电后进入 `WAIT_HELLO`
2. 收到 `SYS.HELLO(session)` 后，锁定 `session_id`，发送 `SYS.HELLO_ACK(session, ...)`
3. 收到 `SYS.CM4_RESOURCE_READY(session, ...)` 后，发送 `SYS.DSP_MODEL_READY(session, ...)`
4. 收到 `CMD.CONFIG_APPLY` 后，返回 `ACK(kind=CMD, code=CONFIG_APPLY)`
5. 收到 `CMD.BUFFER_BIND` 后，返回 `ACK(kind=CMD, code=BUFFER_BIND)`
6. 收到 `SYS.START_STREAM` 后，返回 `ACK(kind=SYS, code=START_STREAM)`，并进入 `RUNNING`
7. 在 `RUNNING` 态处理 `CMD.FRAME_READY` / `CMD.AUDIO_READY`，只打印日志即可

---

## 3. 需要实现的消息

共享协议头：

1. `Algorithm_Models/protocol/control_proto.h`

DSP 至少要能识别和发送下列消息：

### 必收

1. `SYS.HELLO`
2. `SYS.CM4_RESOURCE_READY`
3. `SYS.START_STREAM`
4. `CMD.CONFIG_APPLY`
5. `CMD.BUFFER_BIND`
6. `CMD.FRAME_READY`
7. `CMD.AUDIO_READY`
8. `SYS.HEARTBEAT`

### 必回

1. `SYS.HELLO_ACK`
2. `SYS.DSP_MODEL_READY`
3. `ACK(kind=CMD, code=CONFIG_APPLY)`
4. `ACK(kind=CMD, code=BUFFER_BIND)`
5. `ACK(kind=SYS, code=START_STREAM)`
6. `STATUS` 或 `SYS.HEARTBEAT`

### 可选

1. `NACK`，当 `session_id` 不匹配或状态非法时返回
2. `SYS.RECOVER_DONE`

---

## 4. 推荐处理逻辑

### 4.1 收到 HELLO

1. 提取 `session_id`
2. 如果和当前锁定的 session 不同，则重置内部状态
3. 记录当前 `session_id`
4. 发送 `HELLO_ACK`
5. 进入 `READY`

推荐返回：

1. `boot_reason = WARM_RESET`
2. `feature_level = 1`
3. `run_state = READY`

### 4.2 收到 CM4_RESOURCE_READY

1. 校验 `session_id`
2. 读取 input_type、resource_flags、config_slot_id
3. 不需要真的初始化 AI 模型，只需要模拟内部资源 ready
4. 发送 `DSP_MODEL_READY`

### 4.3 收到 CONFIG_APPLY / BUFFER_BIND

1. 校验 `session_id`
2. 记录参数
3. 返回 `ACK`

如果状态不对，比如尚未收到 `CM4_RESOURCE_READY` 就收到了 `BUFFER_BIND`，建议返回：

1. `NACK(kind=CMD, error=CONTROL_ERR_RESOURCE_NOT_READY)`

### 4.4 收到 START_STREAM

1. 校验 `session_id`
2. 返回 `ACK(kind=SYS, code=START_STREAM)`
3. 进入 `RUNNING`

### 4.5 RUNNING 态

收到 `FRAME_READY(slot)`：

1. 打印 `frame slot ready`
2. 不需要做实际帧处理

收到 `AUDIO_READY(slot)`：

1. 打印 `audio slot ready`
2. 不需要做实际 KWS 推理

收到 `HEARTBEAT`：

1. 可选地回 `STATUS`
2. 或回 `SYS.HEARTBEAT`，回显 `seq`

---

## 5. DSP 侧伪代码

```c
uint8_t g_session_id = 0;
State g_state = WAIT_HELLO;

for (;;) {
    uint32_t msg;

    if (!mailbox_has_data()) {
        continue;
    }

    msg = mailbox_read();

    if (CONTROL_GET_TYPE(msg) == CONTROL_MSG_TYPE_SYS &&
        CONTROL_GET_SUBTYPE(msg) == CONTROL_SYS_SUBTYPE_HELLO) {
        g_session_id = CONTROL_GET_SESSION(msg);
        g_state = READY;
        mailbox_write(CONTROL_SYS_HELLO_ACK(
            g_session_id,
            CONTROL_BOOT_REASON_WARM_RESET,
            1,
            CONTROL_RUN_STATE_READY));
        continue;
    }

    if (!control_msg_session_matches(msg, g_session_id)) {
        continue;
    }

    switch (CONTROL_GET_TYPE(msg)) {
    case CONTROL_MSG_TYPE_SYS:
        handle_sys_message(msg);
        break;
    case CONTROL_MSG_TYPE_CMD:
        handle_cmd_message(msg);
        break;
    default:
        break;
    }
}
```

---

## 6. 联调预期日志

### CM4 侧预期

1. `DSP warm reset triggered, session=0x..`
2. `TX SYS.HELLO`
3. `RX HELLO_ACK`
4. `TX SYS.CM4_RESOURCE_READY`
5. `RX DSP_MODEL_READY`
6. `TX CMD.CONFIG_APPLY`
7. `RX ACK`
8. `TX CMD.BUFFER_BIND`
9. `RX ACK`
10. `TX SYS.START_STREAM`
11. `RX ACK`
12. 进入 `RUNNING`，开始 `FRAME_READY` / `AUDIO_READY`

### DSP 侧预期

1. `HELLO received`
2. `HELLO_ACK sent`
3. `CM4_RESOURCE_READY received`
4. `DSP_MODEL_READY sent`
5. `CONFIG_APPLY acked`
6. `BUFFER_BIND acked`
7. `START_STREAM acked`
8. `FRAME_READY slot=...`
9. `AUDIO_READY slot=...`

---

## 7. 测试说明

当前仓库内已完成并验证：

1. CM4 侧 Demo 已实现
2. CM4 侧目标可编译

当前尚未在仓库内提供完整 DSP 对应代码，因此运行联调前，DSP 需要先按本说明补齐最小状态机实现。
