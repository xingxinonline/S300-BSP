# Subboard_App_Common

这里放的是 Card1、Card3 这类子板 App 共享的“运行骨架”，不是广义 middleware。

更准确地说，它承载的是项目内共享的应用层公共实现：

1. 子板主状态机。
2. 子板 CM4 和 DSP 的控制面交互。
3. 子板 I2C 启动协议寄存器实现。
4. DSP 检测结果到 `subboard_detection_result_t` 的统一适配。

之所以不叫 middleware，有两个原因：

1. 它不是板级驱动和上层应用之间的通用中间件抽象。
2. 它高度绑定本仓库的子板启动协议、邮箱控制面和检测结果格式。

因此本目录命名成 `Common/Subboard_App_Common` 更直接，语义也更窄。

各 App 目录只保留自己的：

1. 本地入口 `Src/main.c`。
2. 身份配置 `Inc/subboard_app_identity.h`。
3. 模型、GDB 脚本、README 和各自构建参数。