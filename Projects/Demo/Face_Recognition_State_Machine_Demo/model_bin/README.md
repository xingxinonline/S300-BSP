# Face_Recognition_State_Machine_Demo model_bin

可选的本地 DSP bin 覆盖目录。

如果本目录下同时存在 `model_dtcm_boot.bin` 和 `model_ptcm_boot.bin`，构建与 GDB 脚本会优先使用这里的产物；否则自动回退到 `Algorithm_Models/Face_Recognition`。
