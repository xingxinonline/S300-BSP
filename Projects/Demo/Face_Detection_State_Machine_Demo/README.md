# Face_Detection_State_Machine_Demo

本 Demo 把现有检测显示链路和控制面状态机结合起来，目标是验证人脸检测场景下的最小闭环：

1. CM4 与 DSP 先完成 session 化控制面握手。
2. CM4 完成摄像头、MM、LCD 初始化后，再向 DSP 宣告资源 ready。
3. DSP 返回 `DSP_MODEL_READY`、`ACK(CONFIG_APPLY)`、`ACK(BUFFER_BIND)`、`ACK(START_STREAM)` 后进入运行态。
4. 运行态下 DSP 只需要发送 `MAILBOX_MSG_TYPE_MULTI` 对应的多目标坐标，CM4 负责叠框显示。

这个 Demo 刻意不引入追踪逻辑，CM4 也不会依赖 `track_id`、`miss_count` 或 Kalman 状态。DSP 只要填充一帧的人脸框列表即可。

## 架构分工

1. CM4：初始化 camera/MM/LCD、驱动状态机、校验 session、显示多目标框。
2. DSP：完成控制面响应、做人脸检测、把 `DetectionResult_t` 写入共享内存、通过 mailbox 通知结果偏移。
3. 数据面：检测结果结构复用 `Algorithm_Models/protocol/detection_proto.h`。
4. 控制面：状态机消息复用 `Algorithm_Models/protocol/control_proto.h`。

## 目录说明

1. `Src/main.c`：板级启动与 app tick。
2. `Src/face_detection_app.c`：CM4 控制面状态机、视频链路初始化。
3. `Src/face_detection_overlay.c`：多目标叠框，只处理检测结果，不处理追踪语义。
4. `dsp_reference/README.md`：DSP 最小实现指引。
5. `dsp_bin/README.md`：DSP 固件放置约定。

## 当前数据面约定

1. DSP 使用 `MAILBOX_MSG_TYPE_MULTI | offset` 通知一帧结果。
2. `offset` 指向 `DSP_DETECTION_BASE_ADDR` 下的 `DetectionResult_t`。
3. `count` 表示本帧目标数，CM4 只绘制 `FACE` 或 `UNKNOWN` 类型框。
4. `track_id`、`vx/vy`、`speed`、`kf_confidence`、`miss_count` 可以全部填 0。

## 构建目标

1. `s300_face_detection_state_machine_demo`
2. `dbg_face_detection_state_machine_demo`
3. `img_s300_face_detection_state_machine_demo`

## 建议联调顺序

1. 先只跑通 `HELLO -> HELLO_ACK`。
2. 再跑通 `CM4_RESOURCE_READY -> DSP_MODEL_READY -> ACK/START_STREAM`。
3. 最后让 DSP 固定输出两三个静态框，验证坐标、镜像、显示方向。
4. 静态框正确后再接入真实人脸检测推理。