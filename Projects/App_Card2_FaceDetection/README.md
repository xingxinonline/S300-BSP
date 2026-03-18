# App_Card2_FaceDetection

该目录提供 gimbal_node 板卡上的正式子板应用，对外语义固定为 Face_Detection 子板：

1. 等待主板完成共享视频资源授权。
2. 初始化本地 MM consumer 路径并拉起 DSP。
3. 通过子板启动协议对外发布人脸检测结果。

## 模型加载

应用按独立 DSP bin 目录加载 Face_Detection 模型：

1. 优先使用本目录 model_bin/ 下的本地产物。
2. 如果本地目录不完整，则回退到 Algorithm_Models/Face_Detection。
3. img_s300_card2_face_detection 与 dbg_card2_face_detection 都会使用当前选中的 DSP bin 目录。

## 协议说明

对外寄存器布局保持与子板启动协议一致：

1. 子板从机地址固定为 0x11。
2. REQUEST / REQUEST_ACK 用于子板向主板申请视频与运行态资源。
3. CMD / CMD_ACK / CMD_RESULT 用于主板驱动子板执行 PREPARE_VIDEO 与 START_DSP。
4. RESULT 区域发布的是 subboard_detection_result_t，其中目标筛选逻辑改为优先输出 Face_Detection 的人脸框。

## 当前实现边界

1. App 私有部分只保留本地入口和身份配置，例如 Src/main.c 与 Inc/subboard_app_identity.h。
2. 子板入口、状态机、DSP 控制流和 I2C 启动骨架已收口到 Projects/Common/Subboard_App_Common。
3. 公共协议头的正式位置已收口到 Projects/Common/Subboard_Protocol；Projects/Demo/Subboard_Bringup_Common 仅保留兼容转发层。

## 构建

1. cmake -B build -G Ninja -DBOARD=gimbal_node
2. ninja -C build s300_card2_face_detection img_s300_card2_face_detection
