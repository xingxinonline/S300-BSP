# App_Card1_HumanDetection

Card1 子板应用：

- 运行 DSP 人形检测模型（Human_Detection）
- 不初始化 OV5640（由主板初始化后通过 CPLD 分发视频）
- 通过 I2C1 从设备对外提供检测结果寄存器

## I2C 协议

- 从机地址: `0x10`
- `0x00` (`STATUS`, 1B): `valid`
- `0x10` (`RESULT`, 32B): `card1_detection_result_t`

`RESULT` 结构与主板 `i2c_cardbus` 兼容，可直接读取人形目标中心点、边界框、速度和置信度。

## 构建

先切换子板编译配置（应用板）：

- 任务: `S300: Switch to App Board (应用板)`

然后构建：

- 任务: `Build`

产物目标：`s300_card1_human_detection`
