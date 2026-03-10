# DSP 侧实现指引：KWS_Control_Protocol_Demo

本文档说明 DSP 侧如何实现与 `s300_kws_control_protocol_demo` 对应的最小联调版本。

这个 Demo 的目标不是改变 KWS 模型本身，而是把：

1. 控制面握手与状态机
2. KWS 共享内存数据面

放到同一个 Demo 里跑通。

---

## 1. 总体原则

DSP 侧需要同时满足两条约束：

1. 控制面遵循 `control_proto.h` 的 session 化握手
2. 数据面继续兼容当前 KWS 共享内存布局与 `dsp_ready_flag / input_ready_flag / output_ready_flag`

也就是说：

1. `HELLO_ACK`、`DSP_MODEL_READY`、`ACK`、`HEARTBEAT` 走 mailbox
2. 音频块与 KWS 结果仍然走共享内存，不通过 mailbox 传大数据

---

## 2. 共享内存布局

建议沿用当前仓库里 KWS Demo 使用的固定地址：

1. `0x44040000`：音频输入块，CM4 写入左声道单通道 PCM
2. `0x44040200`：音频输出块，可选，本 Demo 的 CM4 不依赖它
3. `0x44040400`：`kws_cmd[8]`，DSP 写入 8 类结果分数
4. `0x44040430`：`dsp_ready_flag`
5. `0x44040434`：`input_ready_flag`
6. `0x44040438`：`output_ready_flag`
7. `0x4404043C`：`dsp_calc_cycles`

约束：

1. CM4 不会主动清 `dsp_ready_flag`
2. CM4 会在每轮 session 开始前清 `input_ready_flag` 和 `output_ready_flag`
3. DSP 只有在本轮控制面进入 `RUNNING` 后，才应把 `dsp_ready_flag` 置 1

---

## 3. 控制面最小状态机

建议 DSP 侧状态：

1. `WAIT_HELLO`
2. `READY`
3. `RESOURCE_ACCEPTED`
4. `CONFIGURED`
5. `RUNNING`
6. `ERROR`

建议状态迁移：

1. 上电后进入 `WAIT_HELLO`
2. 收到 `SYS.HELLO(session)` 后锁定 `session_id`，回复 `SYS.HELLO_ACK(session, WARM_RESET, feature=1, READY)`
3. 收到 `SYS.CM4_RESOURCE_READY` 后，检查 `input_type == CONTROL_INPUT_AUDIO`，回复 `SYS.DSP_MODEL_READY`
4. 收到 `CMD.CONFIG_APPLY` 后返回 `ACK(kind=CMD, code=CONFIG_APPLY)`
5. 收到 `CMD.BUFFER_BIND` 后返回 `ACK(kind=CMD, code=BUFFER_BIND)`
6. 收到 `SYS.START_STREAM` 后：
   - 清理数据面状态
   - 把 `dsp_ready_flag = 1`
   - 回复 `ACK(kind=SYS, code=START_STREAM)`
   - 进入 `RUNNING`

---

## 4. RUNNING 状态下的数据面行为

DSP 在 `RUNNING` 状态下建议执行如下逻辑：

1. 轮询 `input_ready_flag`
2. 当 `input_ready_flag == 1` 时，从 `0x44040000` 读取一块音频数据
3. 执行 KWS 前处理与推理
4. 将 8 类结果分数写回 `kws_cmd[8]`
5. 更新 `dsp_calc_cycles`
6. 把 `input_ready_flag` 清 0
7. 把 `output_ready_flag` 置 1

关键点：

1. `input_ready_flag` 与 `output_ready_flag` 的读写都应配合内存屏障
2. `dsp_ready_flag` 在 `RUNNING` 期间应保持为 1，直到 stop/recover/reset
3. 不要在每个音频块上都强依赖 mailbox 往返，避免控制面被高频音频事件淹没

---

## 5. 必须支持的 mailbox 消息

共享协议头：

1. `Algorithm_Models/protocol/control_proto.h`

至少需要支持：

### DSP 必收

1. `SYS.HELLO`
2. `SYS.CM4_RESOURCE_READY`
3. `SYS.START_STREAM`
4. `SYS.HEARTBEAT`
5. `CMD.CONFIG_APPLY`
6. `CMD.BUFFER_BIND`

### DSP 必回

1. `SYS.HELLO_ACK`
2. `SYS.DSP_MODEL_READY`
3. `ACK(kind=CMD, code=CONFIG_APPLY)`
4. `ACK(kind=CMD, code=BUFFER_BIND)`
5. `ACK(kind=SYS, code=START_STREAM)`
6. `SYS.HEARTBEAT`

---

## 6. 推荐伪代码

```c
state = WAIT_HELLO;
session_id = 0;

for (;;) {
    if (mailbox_has_data()) {
        msg = mailbox_read();

        if (is_hello(msg)) {
            session_id = get_session(msg);
            reset_control_state();
            send_hello_ack(session_id);
            state = READY;
            continue;
        }

        if (!session_matches(msg, session_id)) {
            continue;
        }

        handle_control_message(msg);
    }

    if (state == RUNNING && input_ready_flag == 1) {
        process_one_audio_block();
        input_ready_flag = 0;
        output_ready_flag = 1;
    }
}
```

---

## 7. 预期联调日志

### CM4 侧

1. `TX SYS.HELLO`
2. `RX HELLO_ACK`
3. `TX SYS.CM4_RESOURCE_READY`
4. `RX DSP_MODEL_READY`
5. `TX CMD.CONFIG_APPLY`
6. `TX CMD.BUFFER_BIND`
7. `TX SYS.START_STREAM`
8. `Stream start ACK received, audio data plane enabled`
9. 后续打印 heartbeat 与识别结果

### DSP 侧

1. `HELLO received`
2. `HELLO_ACK sent`
3. `CM4_RESOURCE_READY received`
4. `DSP_MODEL_READY sent`
5. `CONFIG_APPLY acked`
6. `BUFFER_BIND acked`
7. `START_STREAM acked`
8. `dsp_ready_flag = 1`
9. 后续持续处理 `input_ready_flag`

---

## 8. DSP bin 输出建议

本 Demo 使用独立的 `dsp_bin/` 目录，建议至少输出以下文件：

1. `model_dtcm_boot.bin`
2. `model_ptcm_boot.bin`
3. `model_sram1_boot.bin`

如果 DSP 链接布局还使用 `sram0` 或 `psram` 段，也可同时提供：

1. `model_sram0_boot.bin`
2. `model_psram_boot.bin`

调试脚本会在 `dsp_firmware_load_point()` 断点处按文件是否存在、是否非空自动加载。

当前仓库里的调试脚本已经改为更稳妥的方式：在 DSP 域保持复位时直接加载 bin，然后释放 DSP 域复位并让 CM4 继续执行，因此 DSP 不再依赖 GDB 运行时命中符号断点才能被加载。

---

## 9. 历史握手失败现象分析

结合当前联调日志，现象是：

1. CM4 侧持续发送 `SYS.HELLO(session)`，直到超时重启 session
2. DSP 串口只打印启动早期的单条 boot 日志，例如 `Hello, world168!`
3. DSP 没有继续打印 `RX HELLO`、`HELLO_ACK sent`、`DSP_MODEL_READY sent`

这类现象说明的不是“DSP 没有启动”，而是：

1. DSP 已经启动，UART 也工作正常
2. 但 DSP 没有真正进入控制面 mailbox 轮询主循环，或者在进入前就卡住/异常
3. 因此 CM4 永远收不到 `HELLO_ACK`

对当前 KWS 场景，最可能的根因有四类：

1. DSP 在进入 `WAIT_HELLO` 之前做了过重的初始化，导致握手窗口内没有及时响应
2. DSP bin 不是最新构建产物，运行的仍是旧固件，日志和源码不一致
3. mailbox 初始化或读写方向不对，导致 DSP 实际没有收到 CM4 的 `HELLO`
4. DSP 的 `sram1` 数据布局与 CM4 约定地址不一致，导致共享区或控制状态被覆盖

---

## 10. DSP 优化建议

### 10.1 把重初始化后移，不要阻塞首个 HELLO_ACK

DSP 在 `main()` 里应优先完成：

1. 最小运行时初始化
2. mailbox 初始化
3. 进入 `WAIT_HELLO`
4. 收到 `HELLO` 后立即回 `HELLO_ACK`

不要在 `HELLO_ACK` 之前执行耗时且可能失败的阶段，例如：

1. VAD 全量初始化
2. KWS 模型参数重建
3. 大块 SRAM 清零或数据搬移
4. 复杂的 profiling、trace、外设自检

推荐分层：

1. `HELLO` 前只做最小控制面 bring-up
2. `CM4_RESOURCE_READY` 后再做模型 runtime 初始化
3. `START_STREAM` 后再把 `dsp_ready_flag` 置 1

### 10.2 把启动日志拆成阶段日志

不要只保留一条总启动日志，建议至少拆成：

1. `boot entry`
2. `mailbox init done`
3. `wait hello`
4. `resource ready received`
5. `kws runtime init start`
6. `kws runtime init done`
7. `start stream acked`

这样可以区分：

1. 是根本没跑到 mailbox
2. 还是卡在模型初始化
3. 还是卡在进入 RUNNING 之后的数据面

### 10.3 控制面先通，再放开数据面

建议 DSP 端严格遵循下面顺序：

1. `HELLO` -> `HELLO_ACK`
2. `CM4_RESOURCE_READY` -> `DSP_MODEL_READY`
3. `CONFIG_APPLY` -> `ACK`
4. `BUFFER_BIND` -> `ACK`
5. `START_STREAM` -> `ACK`
6. 然后再开放 `dsp_ready_flag`

不要在 `WAIT_HELLO` 或 `READY` 阶段就把 `dsp_ready_flag` 置 1。

### 10.4 明确 `sram1` 布局边界

当前 Demo 的共享内存区本身就在 `0x44040000` 附近，而 GDB 也会把 `model_sram1_boot.bin` 直接恢复到这一段地址。

因此 DSP 侧必须确保：

1. `input_block`、`output_block`、`kws_cmd`、各 ready flag 的链接地址与 CM4 约定完全一致
2. `model_sram1_boot.bin` 里的初始化内容不会覆盖掉不该覆盖的共享区布局
3. `.map` 文件中的符号地址和 CM4 文档一致

至少应核对：

1. `input_block == 0x44040000`
2. `kws_cmd == 0x44040400`
3. `dsp_ready_flag == 0x44040430`
4. `input_ready_flag == 0x44040434`
5. `output_ready_flag == 0x44040438`

### 10.5 区分“旧 bin”与“新源码”

如果板上打印的日志和当前源码对不上，优先怀疑 bin 没有更新，而不是优先怀疑协议。

联调前至少确认：

1. `Debug/model_dtcm_boot.bin`
2. `Debug/model_ptcm_boot.bin`
3. `Debug/model_sram1_boot.bin`

已经重新由当前源码编译生成，并且已复制到本 Demo 的 `dsp_bin/` 目录。

---

## 11. DSP 排障清单

当 CM4 一直卡在 `HANDSHAKING` 时，建议按下面顺序排查：

1. 先确认板上 DSP 串口是否只打印了早期 boot 日志
2. 再确认 DSP 是否打印了 `wait hello`
3. 若没有 `wait hello`，说明问题在 mailbox 轮询前的启动路径
4. 若有 `wait hello` 但没有 `RX HELLO`，说明 mailbox 接收方向或基址有问题
5. 若有 `RX HELLO` 但没有 `HELLO_ACK sent`，说明回包路径或发送 FIFO 有问题
6. 若 `HELLO_ACK` 已发送但 CM4 没收到，再回头看 CM4 侧 session 校验和收包基址

推荐最小联调目标：

1. 第一阶段只要求 `HELLO -> HELLO_ACK` 跑通
2. 第二阶段再接 `CM4_RESOURCE_READY -> DSP_MODEL_READY`
3. 第三阶段再接 KWS 数据面

不要在握手还没稳定之前，同时调 mailbox、KWS 模型、VAD 和共享内存结果解析。

当前优先级：先跑通 `HELLO -> HELLO_ACK`，再继续看 `DSP_MODEL_READY` 和数据面。

---

## 12. 最新联调结论

结合最新板级日志，这个 Demo 当前已经验证通过以下链路：

1. CM4 与 DSP 控制面握手稳定完成，包含 `HELLO_ACK`、`DSP_MODEL_READY`、`CONFIG_APPLY ACK`、`BUFFER_BIND ACK`、`START_STREAM ACK`
2. DSP 在 `RUNNING` 状态下持续回传 `STATUS` 与 `HEARTBEAT`
3. 共享内存数据面持续工作，CM4 `chunks` 与 `results` 基本同步增长
4. KWS 结果已经能够在 CM4 侧稳定打印，说明音频采集、DSP 前处理、KWS 推理、结果回写这条链路已端到端跑通

从当前日志看，已识别出的口令至少包括：

1. `pai-zhang-zhaopian`
2. `qidong-gensui`
3. `jieshu-gensui`
4. `kaishi-luxiang`
5. `tingzhi-luxiang`
6. `dakai-buguangdeng`
7. `guanbi-buguangdeng`

这意味着当前问题重心已经不再是：

1. mailbox 基址是否正确
2. DSP 是否收到 `HELLO`
3. `dsp_ready_flag` 是否没有拉起

而更偏向于结果质量和产品化后处理，例如：

1. 同一句口令连续多帧触发时是否要做去抖或合并上报
2. 识别阈值是否要按词条分别调整
3. 是否需要加入命令保持时间窗、冷却时间或投票机制

---

## 13. 当前剩余优化方向

如果后续要继续优化，建议优先做下面三类工作，而不是再回头怀疑控制协议：

1. 结果后处理：对连续高分结果做去抖、锁存或最小间隔限制，避免同一句话打印几十次
2. 阈值调优：按词条统计误触发和漏检，必要时把统一阈值 `90` 改成按关键词单独配置
3. 统计语义澄清：`backpressure` 代表 CM4 提交音频时发现 DSP 尚未消费上一块，并不等同于 `out_of` 这种输出缓冲溢出

以上三项属于结果质量优化，不属于控制协议或 mailbox 通路故障。

