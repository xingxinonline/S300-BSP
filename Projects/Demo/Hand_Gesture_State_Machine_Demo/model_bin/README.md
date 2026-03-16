# Hand_Gesture_State_Machine_Demo DSP Binary Files

将手势检测 Demo 对应的 DSP 二进制文件放到本目录：

1. `model_dtcm_boot.bin`
2. `model_ptcm_boot.bin`
3. `model_sram0_boot.bin` 或 `model_sram1_boot.bin`
4. 如有需要，再放 `model_psram_boot.bin`

联调时：

1. 先构建 `s300_hand_gesture_state_machine_demo`
2. 运行 `ninja dbg_hand_gesture_state_machine_demo` 启动联调
3. 或运行 `ninja img_s300_hand_gesture_state_machine_demo` 生成整包镜像

如果目录下没有 DSP bin，CM4 侧仍可单独编译；只是 GDB 脚本和整包镜像不会预装手势 DSP 固件。