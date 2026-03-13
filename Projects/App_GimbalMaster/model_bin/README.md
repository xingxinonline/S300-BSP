# App_GimbalMaster model_bin

把主板 KWS App 专用的 DSP bin 放到本目录。

如果希望本地覆盖路径也参与版本管理，请同时放入：

1. model_info.json

最少需要：

1. model_dtcm_boot.bin
2. model_ptcm_boot.bin

如果模型还依赖 SRAM 段，也建议一并放入：

1. model_sram0_boot.bin
2. model_sram1_boot.bin

如果本目录不完整，CMake 会自动回退到 Algorithm_Models/Keyword_Spotting。

构建时会生成当前实际选中模型的 manifest，并在主板启动日志中打印 model id、version、date 和目录路径。