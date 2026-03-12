# Subboard_MM_Consumer_Demo

这个 Demo 用来验证云台子板 gimbal_node 的最小人脸检测闭环，职责边界如下：

1. 子板 CM4 负责本板 MM 路径初始化、与主板通过 I2C1 协商资源、拉起本板 DSP，并转发 DSP 提出的主板侧同步请求。
2. 子板 DSP 负责人脸检测推理、控制面握手、结果写入共享内存，以及在运行态按需请求主板执行 MM 运行态使能。
3. 主板不直接参与子板 DSP 控制面握手，但负责响应子板转发过来的 MM 相关请求。

## 目录说明

1. Src/main.c：子板 CM4 启动入口、I2C 启动状态机、MM 启动、START_DSP 命令处理。
2. Src/subboard_dsp_ctrl.c：子板 CM4 和本板 DSP 的 mailbox 控制面状态机。
3. model_bin/README.md：本 Demo 本地 DSP 镜像目录约定。
4. dsp_reference/README.md：DSP 侧最小实现约定和联调顺序。

## DSP 镜像目录约定

本 Demo 现在支持两种 DSP 镜像来源，优先级如下：

1. 优先使用本目录下的 model_bin/。
2. 如果本地 model_bin/ 不完整，则回退到 Algorithm_Models/Face_Detection。

本地 model_bin/ 至少需要以下文件：

1. model_ptcm_boot.bin
2. model_dtcm_boot.bin

如果 DSP 还依赖 SRAM 或 PSRAM 数据段，建议一并放入：

1. model_sram0_boot.bin
2. model_sram1_boot.bin
3. model_psram_boot.bin

## 当前打包行为

CMake 会为 s300_subboard_mm_consumer_demo 生成带 DSP 固件的 S300 镜像。典型命令：

1. cmake -B build -G Ninja -DBOARD=gimbal_node
2. ninja -C build s300_subboard_mm_consumer_demo img_s300_subboard_mm_consumer_demo

最终镜像输出到 build/s300_subboard_mm_consumer_demo_gimbal_node.bin。

## 运行时控制面摘要

子板 DSP 需要和 CM4 完成以下控制面链路：

1. SYS.HELLO_ACK
2. SYS.DSP_MODEL_READY
3. ACK(CONFIG_APPLY)
4. ACK(BUFFER_BIND)
5. ACK(START_STREAM)

运行态结果上报沿用 detection_proto.h，运行态同步请求沿用 subboard_runtime_proto.h。子板 DSP 仍然依赖完整视频资源标志，只是这些标志由子板 CM4 汇总后再上报：本地 `MM_READY` 来自子板自身初始化，`CAMERA_READY` 和 `LCD_READY` 来自主板视频链路已经授权就绪的同步语义。
