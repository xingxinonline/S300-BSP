# Subboard_Video_Bringup_Master_Demo

这个 Demo 运行在 gimbal_master 上，职责是主板侧的视频资源拉起、板间启动协商、子板状态轮询，以及把子板检测结果画到主板 LCD 上。

## 模块划分

1. Src/main.c：板级初始化、SysTick 建立、app 调度入口。
2. Src/master_demo_app.c：主板 I2C 主机、启动状态机、运行态轮询、子板结果读取。
3. Src/master_detection_overlay.c：LCD 叠框绘制、清框、超时回收与显示刷新。

## 启动前提

1. 主板必须先完成 OV5640 预初始化、MM PLL 与视频链路初始化，随后才能应答子板的 REQUEST_MASTER_MM_ENABLE。
2. 进入运行态后，如果子板通过 I2C 发来 REQUEST_MASTER_MM_RUNTIME，主板负责执行显示侧 MM 运行态使能。
3. LCD 叠框修改 alpha/color buffer 后，需要显式触发显示刷新寄存器，否则框可能不会及时可见。

## 维护原则

1. main 只保留板级启动和 app tick，不再承载具体业务状态机。
2. 启动协商与叠框逻辑拆分，减少 I2C 状态处理和显示写显存之间的耦合。
3. 新增主板侧功能时，优先放入 master_demo_app 或 master_detection_overlay，而不是重新回填 main。

## 日志级别

默认日志级别为 INFO，会输出关键状态变化、请求应答和告警，不输出 DEBUG 级高频轮询细节。

如需打开完整联调日志，可在配置时覆盖：

1. cmake -B build -G Ninja -DBOARD=gimbal_master -DMASTER_DEMO_LOG_LEVEL=DEBUG

可选值：

1. WARN：只输出告警和失败
2. INFO：默认值，输出关键流程和告警
3. DEBUG：输出心跳、结果变化和轮询细节
