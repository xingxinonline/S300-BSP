# AI 算法 Demo 运行说明

本文档说明 S300 BSP 中 AI 算法相关示例的运行方法。

## 1. 异构运行机制

S300 采用异构协同模式：
*   **Cortex-M4**: 运行主程序 (App)，控制外设 (Camera, LCD, UART)。
*   **AI 子系统**: 独立运行模型固件 (Firmware)，负责推理。

调试时，GDB 脚本会自动分别加载 M4 ELF 和 AI Model BIN 到指定内存及核心。

## 2. 环境准备

*   **硬件**: S300 开发板 (连接 Camera & LCD)，Debug UART 连接 PC。
*   **软件**: OpenOCD 已连接，串口工具打开 (波特率 921600)。

## 3. 运行检测类 Demo (MM_Test)

本项目提供统一的测试工程 `MM_Test_Demo`，配合不同的构建目标可加载不同模型。

| 功能 | 目标名称 (Target) | 说明 |
| :--- | :--- | :--- |
| **人脸检测** | `dbg_mm_test_face-detection` | 框出人脸位置 |
| **人形检测** | `dbg_mm_test_human-detection` | 检测上半身/全身 |
| **手势识别** | `dbg_mm_test_hand-gesture` | 识别手掌、拳头等手势 |

**运行指令 (任选其一):**

```bash
# 运行人脸检测
ninja -C build dbg_mm_test_face-detection

# 运行人形检测
ninja -C build dbg_mm_test_human-detection
```

> **注意**: 切换不同模型时无需重新编译 M4 代码，只需运行对应的 `dbg_` 目标，GDB 会自动替换加载的 AI 固件。

## 4. 人脸识别 Demo (Face Recognition)

包含设备端推理与 PC 端交互两部分。

### 4.1 设备端

加载人脸识别固件：

```bash
ninja -C build dbg_face_recognition_demo_model
```

启动后串口将打印 `[FACE] Init OK`。

### 4.2 通过上位机注册人脸

位于 `Projects/Demo/Face_Recognition_Demo/host_app` 的 Python 工具用于录入人脸。

```bash
cd Projects/Demo/Face_Recognition_Demo/host_app
uv sync                # 安装依赖
uv run app.py --port COMx  # 运行 (替换 COMx 为实际端口)
```

在上位机输入姓名并点击注册，完成后开发板 LCD 将实时显示识别结果。

## 5. 互动眼球 Demo

结合 LVGL 界面与人脸检测，实现眼球跟随效果。

**运行指令:**

```bash
ninja -C build dbg_display_face-detection
```
