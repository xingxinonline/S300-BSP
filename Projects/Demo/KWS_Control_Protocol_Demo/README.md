# KWS_Control_Protocol_Demo

本 Demo 把当前仓库里已验证的 KWS 音频采集链路，与新的 CM4-DSP 控制协议状态机结合起来。

目标是验证两件事：

1. CM4 与 DSP 通过 `HELLO -> RESOURCE_READY -> CONFIG/BUFFER -> START_STREAM` 建立可信控制链路
2. 在 `RUNNING` 状态下，CM4 继续沿用 KWS Demo 的共享内存音频搬运方式，把音频块送给 DSP 处理

本 Demo 的控制面使用 [Algorithm_Models/protocol/control_proto.h](Algorithm_Models/protocol/control_proto.h)，数据面复用 KWS 共享内存布局。

最新联调状态：

1. `HELLO -> CM4_RESOURCE_READY -> CONFIG_APPLY -> BUFFER_BIND -> START_STREAM -> RUNNING` 已稳定跑通
2. CM4 已能持续收到 DSP `STATUS` 与 `HEARTBEAT`
3. CM4 已能打印实际关键词识别结果，例如 `pai-zhang-zhaopian`、`qidong-gensui`、`jieshu-gensui`、`kaishi-luxiang`、`dakai-buguangdeng`、`guanbi-buguangdeng`
4. 因此当前 Demo 已经不是“仅协议验证”，而是“控制面 + 数据面 + KWS 结果上报”端到端验证通过

相关文件：

1. `Src/main.c`：CM4 控制面状态机与启动流程
2. `Src/kws_control_stream.c`：KWS 音频搬运与结果轮询
3. `dsp_reference/README.md`：DSP 侧实现指引
4. `dsp_bin/README.md`：本 Demo 的 DSP bin 放置规范

构建目标：

1. `s300_kws_control_protocol_demo`
2. `dbg_kws_control_protocol_demo`
3. `img_s300_kws_control_protocol_demo`

说明：

1. 本 Demo 使用独立的 `dsp_bin/` 目录，不与其他 Demo 共用 DSP 固件
2. CM4 侧会初始化音频采集链路，并在 `RUNNING` 状态下通过共享内存标志与 DSP 交换音频块和 KWS 结果
3. 调试脚本会在 DSP 域保持复位时直接加载本 Demo 的 DSP bin，再释放 DSP 域复位
4. 如果 CM4 卡在 `HANDSHAKING`，优先检查 DSP 是否只打印了早期 boot 日志而没有进入 `WAIT_HELLO`；详细分析见 `dsp_reference/README.md`
5. 运行时 `backpressure` 很高但 `out_of=0` 时，通常表示 CM4 多次轮询时发现 `input_ready_flag` 尚未被 DSP 清空，这是节拍差异带来的忙等计数，不等同于音频输出环形缓冲溢出
6. CM4 当前已增加结果去抖和按关键词独立阈值：同一关键词需要先连续两帧过自身阈值，且只有超过该关键词最小 chunk 间隔后才会再次打印，避免一句话刷几十行
