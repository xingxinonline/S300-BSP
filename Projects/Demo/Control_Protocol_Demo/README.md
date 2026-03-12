# Control_Protocol_Demo

本 Demo 是一个纯控制面联调 Demo，用于验证 CM4 与 DSP 之间的 session 化 mailbox 协议。

它不做真实 AI 推理，只做下面几件事：

1. CM4 触发 DSP warm reset，并递增 `session_id`
2. CM4 清空 mailbox 中的旧消息，只接受当前 session 的控制消息
3. CM4 按状态机发送 `HELLO`、`CM4_RESOURCE_READY`、`CONFIG_APPLY`、`BUFFER_BIND`、`START_STREAM`
4. 进入 `RUNNING` 后，CM4 周期性发送 `HEARTBEAT`、`FRAME_READY`、`AUDIO_READY`

本 Demo 适合联调：

1. DSP 先于 CM4 上电启动的情况
2. 历史 mailbox 消息污染当前轮次的情况
3. 视频/音频统一控制面的状态机流程

相关文件：

1. `Src/main.c`：CM4 侧伪状态机实现
2. `algorithm_reference/README.md`：DSP 侧实现指引
3. `model_bin/README.md`：DSP bin 放置约定
4. `Algorithm_Models/protocol/control_proto.h`：共享协议头

构建目标：

1. `s300_control_protocol_demo`
2. `dbg_control_protocol`
3. `img_s300_control_protocol_demo`

说明：

1. 该 Demo 已进行 CM4 侧编译验证
2. 运行时需要 DSP 按照 `algorithm_reference/README.md` 实现对应的协议响应
3. 本 Demo 使用独立的 `model_bin/` 目录存放 DSP 镜像，避免与其他 Demo 混用
4. 替换 `model_bin/` 中的 bin 后，无需手动重新配置 CMake；重新执行 `dbg_control_protocol` 即会刷新加载脚本
