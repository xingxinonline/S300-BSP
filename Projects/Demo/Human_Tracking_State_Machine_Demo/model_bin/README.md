# Human_Tracking_State_Machine_Demo DSP Binary Files

请将本 Demo 对应的 DSP 编译产物放到当前目录。

推荐文件名：

1. model_dtcm_boot.bin：加载到 0x44800000
2. model_ptcm_boot.bin：加载到 0x44A00000
3. model_sram0_boot.bin：可选，加载到 0x44000000
4. model_sram1_boot.bin：推荐，加载到 0x44040000
5. model_psram_boot.bin：可选，按需加载

使用方式：

1. 将 DSP bin 拷贝到本目录。
2. 运行 ninja dbg_human_tracking_state_machine_demo 启动联调。
3. 或运行 ninja img_s300_human_tracking_state_machine_demo 生成整包镜像。

如果只是替换 bin 文件，不需要手动重新配置 CMake。
