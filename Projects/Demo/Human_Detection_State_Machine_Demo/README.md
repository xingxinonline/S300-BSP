# Human_Detection_State_Machine_Demo

本 Demo 把现有检测显示链路和 CM4-DSP 控制面状态机结合起来，目标是验证人形检测场景下的最小闭环：

1. CM4 与 DSP 先完成带 session 的控制面握手。
2. CM4 完成摄像头、MM、LCD 初始化后，通过 SYS.CM4_RESOURCE_READY 向 DSP 宣告视频资源就绪。
3. DSP 依次返回 SYS.DSP_MODEL_READY、ACK(CONFIG_APPLY)、ACK(BUFFER_BIND)、ACK(START_STREAM)，CM4 才进入运行态。
4. 运行态下 DSP 发送 MAILBOX_MSG_TYPE_MULTI | offset 上报检测结果；如需 MM 运行态生效，则额外发送统一运行时通知请求，由 CM4 根据板型决定是否同时触发 CORE_REG_UPDATE 和 SPI_REG_UPDATE。
5. CM4 负责写 MM/LCD 相关寄存器，DSP 不直接改主板视频寄存器。

这个 Demo 刻意不引入追踪逻辑。CM4 不依赖 track_id、miss_count 或 Kalman 状态；DSP 只要填充一帧人体框列表即可。

## 架构分工

1. CM4：初始化 camera/MM/LCD、驱动状态机、校验 session、接收 DSP 的 MM 运行态使能请求并按板型触发对应寄存器同步、显示多目标框。
2. DSP：完成控制面响应、做人形检测、把 DetectionResult_t 写入共享内存、通过 mailbox 通知结果偏移，并在需要时发送统一的 MM 运行态使能请求。
3. 数据面：检测结果结构复用 Algorithm_Models/protocol/detection_proto.h。
4. 控制面：状态机消息复用 Algorithm_Models/protocol/control_proto.h。

## 目录说明

1. Src/main.c：板级启动、SysTick 和 app tick。
2. Src/human_detection_app.c：CM4 控制面状态机、视频链路初始化。
3. Src/human_detection_overlay.c：多目标叠框，只处理人形检测结果，不处理追踪语义。
4. algorithm_reference/README.md：DSP 最小实现指引。
5. model_bin/README.md：DSP 固件放置约定。

## 当前数据面约定

1. DSP 使用 MAILBOX_MSG_TYPE_MULTI | offset 通知一帧结果。
2. offset 指向 DSP_DETECTION_BASE_ADDR 下的 DetectionResult_t。
3. count 表示本帧目标数，CM4 只绘制 PERSON 或 UNKNOWN 类型框。
4. track_id、vx/vy、speed、kf_confidence、miss_count 可以全部填 0。
5. 协议版本兼容 v2.x 和 v3.x 主版本；magic、version、count 任一不合法时本帧会被丢弃。

## 构建目标

1. s300_human_detection_state_machine_demo
2. dbg_human_detection_state_machine_demo
3. img_s300_human_detection_state_machine_demo

## 建议联调顺序

1. 先只跑通 HELLO -> HELLO_ACK。
2. 再跑通 CM4_RESOURCE_READY -> DSP_MODEL_READY -> ACK(CONFIG_APPLY) -> ACK(BUFFER_BIND) -> ACK(START_STREAM)。
3. 然后验证 DSP 发送统一的 HD_RT_MAKE_MM_ENABLE_REQ() 后，CM4 是否按当前板型正确完成 MM 运行态使能。
4. 最后让 DSP 固定输出两三个静态框，验证坐标、镜像、显示方向。
5. 静态框正确后再接入真实人形检测推理。
