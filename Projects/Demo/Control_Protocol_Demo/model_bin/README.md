# Control_Protocol_Demo DSP Binary Files

请将本 Demo 对应的 DSP 工程编译产物放置在本目录下。

不同 Demo 的 DSP 固件不应混放，因此 `Control_Protocol_Demo` 使用独立的 `model_bin/` 目录。

## 期望文件

1. `model_ptcm_boot.bin`：DSP 程序代码，加载地址 `0x44A00000`
2. `model_dtcm_boot.bin`：DSP 数据段，加载地址 `0x44800000`

## 使用方式

1. 将 DSP bin 拷贝到当前目录
2. 运行 `ninja dbg_control_protocol`
3. 或运行 `ninja img_s300_control_protocol_demo` 生成带本 Demo DSP 固件的整包镜像
4. 如果只是替换 bin 文件，不需要手动重新配置 CMake

## 说明

1. 本目录仅服务于 `Projects/Demo/Control_Protocol_Demo`
2. 调试脚本会从本目录自动加载 DSP bin
3. 镜像打包流程会将本目录作为 `DSP_DIR` 输入
