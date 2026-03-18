# App_GimbalMaster

该目录提供 `gimbal_master` 板卡的正式主控应用。应用在同一条 CM4 主循环内维护两条业务链路，但启动顺序经过显式编排：先把子板推进到运行态，再拉起主板本地 KWS。

1. 面向子板的人机协调与启动控制。
2. 主板本地 KWS 控制面与音频数据面。

最终目标不是展示参考工程，而是形成稳定的主板应用行为：主板先主动驱动子板进入视频就绪、DSP 启动和结果上报状态，再启动本板 KWS。

## 职责边界

1. 保持本板 KWS 的 CM4-DSP 握手顺序：`HELLO -> CM4_RESOURCE_READY -> CONFIG_APPLY -> BUFFER_BIND -> START_STREAM -> RUNNING`。
2. 轮询子板启动寄存器，响应 `REQUEST_MASTER_MM_ENABLE`、`REQUEST_MASTER_MM_RUNTIME` 等请求。
3. 在主板侧准备共享视频路径后，下发 `PREPARE_VIDEO` 与 `START_DSP`。
4. 读取子板发布的检测结果，并在主板本地显示链路上完成叠框。

## 模型加载

应用按独立 DSP bin 目录装载 KWS 模型：

1. 优先使用本目录 `model_bin/` 下的本地产物。
2. 如果本地目录不完整，则回退到 `Algorithm_Models/Keyword_Spotting`。
3. `img_s300_gimbal_master` 与 `dbg_gimbal_master` 均使用当前解析出的 DSP bin 目录。

## 构建

1. `cmake -B build -G Ninja -DBOARD=gimbal_master`
2. `ninja -C build s300_gimbal_master img_s300_gimbal_master`
3. 如需在不挂相机的情况下验证云台动作，可额外打开 `-DMASTER_GIMBAL_DEBUG_BOOT_DEMO=ON`，主板 App 会在启动后自动执行一次固定姿态调试序列。

## 启动顺序

1. 主板初始化 I2C 协调服务。
2. 主板等待子板上线并响应 `REQUEST_MASTER_MM_ENABLE`。
3. 主板完成本地视频准备并下发 `PREPARE_VIDEO`、`START_DSP`。
4. 子板进入 `RUNNING` 后，主板再初始化音频、mailbox、DSP PLL 和 KWS 控制面。
5. 如果子板在等待窗口内未进入 `RUNNING`，主板会按超时降级路径直接启动本地 KWS，避免调试阶段无限等待。默认等待 3 秒，可通过构建配置覆盖。
6. KWS 运行期的 `STATUS`、heartbeat 和 `chunks/results/backpressure` 统计日志都支持通过构建配置调节频率，默认已经按联调场景做了降噪。
7. 之后主板持续并行执行本板 KWS 与子板结果轮询。

## 运行结构

1. `Src/main.c` 负责先调度子板协调服务，再在子板进入 `RUNNING` 后启动主板 KWS 状态机。
2. `Src/master_demo_app.c` 负责 I2C 启动协调，以及对 Card1/Card3 子板模块的统一装配与调度。
3. `Src/app_card1_result_handler.c` 与 `Src/app_card3_result_handler.c` 分别负责 Card1 检测结果呈现和 Card3 手势结果动作映射。
4. `Src/app_gimbal_control.c` 负责云台底层执行接口，当前已支持 parking/tracking pose、绝对角度移动、preset 和姿态读回。
5. `Src/app_gimbal_debug.c` 提供可选的主板开机云台调试序列，用于无相机挂载场景下的动作联调。
6. `Projects/Demo/KWS_Control_Protocol_Demo/Src/kws_control_stream.c` 负责 KWS 数据面缓冲与流控制。
7. `Projects/Demo/Audio_KWS_Demo/Src/audio_app.c` 与 `audio_codec.c` 负责音频采集路径。

更完整的主板、子板和联合状态机说明见 `docs/STATE_MACHINES.md`。
后续开发阶段建议见 `docs/DEVELOPMENT_ROADMAP.md`。
当前阶段的云台控制优先级与调试方式见 `docs/GIMBAL_CONTROL_PHASE_PLAN.md`。
v0.1 基线验证步骤见 `docs/V0_1_VALIDATION_CHECKLIST.md`。
