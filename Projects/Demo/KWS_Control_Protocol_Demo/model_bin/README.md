# KWS_Control_Protocol_Demo DSP Binary Files

请将本 Demo 对应的 DSP 编译产物放到当前目录。

这是 `KWS_Control_Protocol_Demo` 自己的 DSP 固件目录，不应与 `KWS_CM4_Demo`、`Audio_KWS_Demo` 或其他 Demo 混用。

## 推荐文件名

1. `model_dtcm_boot.bin`：加载到 `0x44800000`
2. `model_ptcm_boot.bin`：加载到 `0x44A00000`
3. `model_sram0_boot.bin`：加载到 `0x44000000`，可选
4. `model_sram1_boot.bin`：加载到 `0x44040000`，推荐
5. `model_psram_boot.bin`：按需加载，可选

## 使用方式

1. 将 DSP bin 拷贝到当前目录
2. 运行 `ninja dbg_kws_control_protocol_demo`
3. 或运行 `ninja img_s300_kws_control_protocol_demo` 生成整包镜像

如果只是替换 bin 文件，不需要手动重新配置 CMake。
