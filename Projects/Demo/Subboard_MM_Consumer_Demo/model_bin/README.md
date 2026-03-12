# Subboard DSP Bin Layout

把子板人脸检测 DSP 固件放到本目录，文件命名与仓库内其他 DSP Demo 保持一致：

1. model_ptcm_boot.bin：DSP 程序代码，加载到 0x44A00000。
2. model_dtcm_boot.bin：DSP 数据段，加载到 0x44800000。
3. model_sram0_boot.bin：可选，加载到 0x44000000。
4. model_sram1_boot.bin：可选，按 DSP 链接脚本决定是否使用。
5. model_psram_boot.bin：可选，按 DSP 模型实际需要决定是否使用。

## 使用规则

1. 如果本目录同时存在 model_ptcm_boot.bin 和 model_dtcm_boot.bin，Subboard_MM_Consumer_Demo 会优先使用本目录生成最终镜像。
2. 如果这两个核心文件缺失，CMake 会自动回退到 Algorithm_Models/Face_Detection 作为 DSP 镜像来源。
3. 建议把和当前子板协议严格匹配的 DSP 产物放在这里，避免直接复用通用模型目录时把实验性修改混到公共模型资产里。

## 推荐做法

1. DSP 侧每次改控制面协议或共享内存布局后，都导出一组新的 boot bin 到本目录。
2. CM4 和 DSP 联调时，以本目录为准，保证构建结果可复现。
3. 如果只是沿用标准人脸检测模型且没有 DSP 私有改动，可以暂时依赖 Algorithm_Models/Face_Detection 的回退路径。
