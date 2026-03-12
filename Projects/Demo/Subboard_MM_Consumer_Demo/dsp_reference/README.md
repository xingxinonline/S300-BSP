# Subboard DSP Implementation Guide

这份说明面向子板 DSP 实现者，目标是把 DSP 侧逻辑约束到当前 Subboard_MM_Consumer_Demo 能稳定消费的最小集合。

## 1. DSP 职责

1. 响应子板 CM4 发来的控制面消息。
2. 在模型和内部资源 ready 后上报 SYS.DSP_MODEL_READY。
3. 接受 CONFIG_APPLY、BUFFER_BIND、START_STREAM，并返回 ACK。
4. 运行态把检测结果写到共享内存，再通过 mailbox 通知 CM4。
5. 当主板侧视频链路需要刷新时，只发送统一 MM 运行态使能请求。

## 2. 必须完成的最小握手顺序

DSP 上电后，至少要完成以下顺序：

1. 接收 SYS.HELLO 并回 SYS.HELLO_ACK。
2. 在收到 SYS.CM4_RESOURCE_READY 后回 SYS.DSP_MODEL_READY。
3. 收到 CMD.CONFIG_APPLY 后回 ACK(CONFIG_APPLY, OK)。
4. 收到 CMD.BUFFER_BIND 后回 ACK(BUFFER_BIND, OK)。
5. 收到 SYS.START_STREAM 后回 ACK(START_STREAM, OK)。

如果任何一步失败，应该回 NACK，让 CM4 进入错误态并重新发起会话。

## 3. Session 约束

1. 所有 ACK、NACK、STATUS、HEARTBEAT 都必须带回当前 session。
2. DSP 不应继续处理旧 session 下尚未消费完的命令。
3. CM4 触发 warm reset 后，DSP 应清空旧会话上下文。

## 4. 运行态结果上报

当前 CM4 已支持两种结果上报方式：

1. MAILBOX_MSG_TYPE_MULTI | offset，对应 DetectionResult_t。
2. MAILBOX_MSG_TYPE_SINGLE | offset，对应 DetectionBox_t。

推荐优先使用 MULTI 方式，因为它和现有人脸检测协议更一致，也方便后续扩展多目标。

## 5. 运行态同步请求

当前 gimbal_node 约束如下：

1. DSP 发送 `SUBBOARD_RT_MAKE_MM_ENABLE_REQ()`。
2. 子板 CM4 不再区分 CORE 或 SPI 请求类型，而是统一转成主板侧 `REQUEST_MASTER_MM_RUNTIME`。
3. 主板 CM4 根据板型决定是否只写核心寄存器，或同时补 SPI 寄存器同步。

## 5.1 资源标志语义

子板 DSP 仍然应该依赖 `SYS.CM4_RESOURCE_READY` 里的完整视频资源标志，而不是只看本地 `MM_READY`：

1. `CONTROL_RESOURCE_MM_READY` 表示子板本地 MM 路径已经初始化完成。
2. `CONTROL_RESOURCE_CAMERA_READY` 表示主板侧摄像头链路已经准备完成，并由子板 CM4 同步汇总给 DSP。
3. `CONTROL_RESOURCE_LCD_READY` 表示主板侧显示/视频输出链路已经准备完成，并由子板 CM4 同步汇总给 DSP。

也就是说，子板 DSP 对资源标志的依赖关系和单板人脸检测仍保持一致，只是 camera/lcd 资源的拥有者从“本板 CM4”变成了“主板完成后由子板 CM4 转述”。

## 6. 共享内存与协议来源

1. 检测结果结构：Algorithm_Models/protocol/detection_proto.h。
2. 控制面协议：Algorithm_Models/protocol/control_proto.h。
3. 子板运行态同步协议：Projects/Demo/Subboard_Bringup_Common/subboard_runtime_proto.h。

DSP 侧不要私自改这些结构的字段布局；如果确实需要改，必须同步更新 CM4 侧解析逻辑和本 Demo 文档。

## 7. 最小联调建议

1. 先只实现 HELLO 和 HELLO_ACK。
2. 再实现 DSP_MODEL_READY 和三段 ACK 链路。
3. 然后用固定静态框验证共享内存结果上报。
4. 最后再接入真实人脸检测模型推理。

在静态框阶段就应该验证：

1. 子板 CM4 能进入 RUNNING。
2. 主板可以收到子板的人脸结果摘要。
3. 运行态只出现统一 MM 运行态使能请求，不再区分 CORE 和 SPI 两条 DSP 协议。
