# App_Card1_HumanDetection

该目录提供 `gimbal_node` 板卡上的正式子板应用。当前实现已经把子板入口、状态机、DSP 控制流和 I2C 启动从 `Projects/Demo/Subboard_MM_Consumer_Demo` 收口到本目录私有代码，对外语义固定为 Human_Detection 子板：

1. 等待主板完成共享视频资源授权。
2. 初始化本地 MM consumer 路径并拉起 DSP。
3. 通过子板启动协议对外发布人形检测结果。

## 模型加载

应用按独立 DSP bin 目录加载 Human_Detection 模型：

1. 优先使用本目录 `model_bin/` 下的本地产物。
2. 如果本地目录不完整，则回退到 `Algorithm_Models/Human_Detection`。
3. `img_s300_card1_human_detection` 与 `dbg_card1_human_detection` 都会使用当前选中的 DSP bin 目录。

## 协议说明

对外寄存器布局保持与子板启动协议一致：

1. 子板从机地址仍为 `0x10`。
2. `REQUEST` / `REQUEST_ACK` 用于子板向主板申请视频与运行态资源。
3. `CMD` / `CMD_ACK` / `CMD_RESULT` 用于主板驱动子板执行 `PREPARE_VIDEO` 与 `START_DSP`。
4. `RESULT` 区域发布的是 `subboard_detection_result_t`，其中目标筛选逻辑改为优先输出 Human_Detection 的人体框。

## 当前实现边界

1. `Src/main.c`、`Src/subboard_mm_app*.c`、`Src/subboard_dsp_*.c`、`Src/subboard_startup_i2c.c` 均为 App 私有实现。
2. `Src/subboard_detection_adapter.c` 负责把 DSP 输出结果转换为对主板统一发布的结果格式。
3. 公共协议结构仍复用 `Projects/Demo/Subboard_Bringup_Common`，以保持主板/子板寄存器协议一致。

主板、子板和联合状态机说明见 `../App_GimbalMaster/docs/STATE_MACHINES.md`。

## 构建

1. `cmake -B build -G Ninja -DBOARD=gimbal_node`
2. `ninja -C build s300_card1_human_detection img_s300_card1_human_detection`
