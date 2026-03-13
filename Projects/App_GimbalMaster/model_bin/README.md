# App_GimbalMaster model_bin

把主板 KWS App 专用的 DSP bin 放到本目录。

最少需要：

1. model_dtcm_boot.bin
2. model_ptcm_boot.bin

如果模型还依赖 SRAM 段，也建议一并放入：

1. model_sram0_boot.bin
2. model_sram1_boot.bin

如果本目录不完整，CMake 会自动回退到 Algorithm_Models/Keyword_Spotting。