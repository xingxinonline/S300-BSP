# App_Card3_Gesture

该目录提供 gimbal_node 板卡上的 Card3 手势子板应用。实现方式参考两条现有链路组合而来：

1. 子板启动、I2C、DSP 控制骨架复用 App_Card1_HumanDetection 的正式子板 App 架构。
2. DSP 控制面与检测语义对齐 Hand_Gesture_State_Machine_Demo，对外发布手势结果。

## 模型加载

应用按独立 DSP bin 目录加载 Hand_Gesture 模型：

1. 优先使用本目录 model_bin 下的本地产物。
2. 如果本地目录不完整，则回退到 Algorithm_Models/Hand_Gesture。
3. img_s300_card3_gesture 与 dbg_card3_gesture 都会使用当前选中的 DSP bin 目录。

## 协议说明

对外寄存器布局保持与子板启动协议一致：

1. 子板从机地址为 0x12。
2. REQUEST / REQUEST_ACK 用于子板向主板申请视频与运行态资源。
3. CMD / CMD_ACK / CMD_RESULT 用于主板驱动子板执行 PREPARE_VIDEO 与 START_DSP。
4. RESULT 区域发布的是 subboard_detection_result_t，其中 type 优先输出 PALM/PEACE，并同步填写 gesture_type 供主板 Card3 桥接逻辑消费。

## 当前实现边界

1. 工程目录维护 Card3 自身的入口代码、身份配置、构建、GDB 脚本和模型目录。
2. 子板入口骨架、状态机、DSP 控制流和 I2C 启动当前复用 Projects/Common/Subboard_App_Common 的共享实现。
3. 结果筛选逻辑切到手势模式，只向主板发布手势类目标。
4. 公共协议头的正式位置已收口到 Projects/Common/Subboard_Protocol；旧的 Demo 目录路径只保留兼容用途。

## 构建

1. cmake -B build -G Ninja -DBOARD=gimbal_node
2. ninja -C build s300_card3_gesture img_s300_card3_gesture