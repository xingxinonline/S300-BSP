# AI 模型 Demo 运行指南

本指南详细介绍如何运行 S300 BSP 提供的各类 AI 模型演示程序，包括人脸检测、人形检测、手势识别及人脸识别互动应用。

## 1. 简介

S300 的 AI Demo 采用 **M4 + AI 算力子系统** 异构协同模式：
1.  **M4 (Host)**: 运行主控逻辑、摄像头采集、显示绘制及 UART 通信。
2.  **AI Subsystem**: 运行专用 `.bin` 固件，负责深度学习模型的推理运算。

调试时，GDB 会自动将 M4 固件（ELF）加载到 SRAM，并将对应的 AI 模型固件加载到特定内存区域（SRAM0/PSRAM/DTCM）。

## 2. 准备工作

在运行任何 Demo 前，请确保：
1.  **硬件连接**：
    *   S300 开发板已通过 USB 连接 PC（确保 OpenOCD 可识别）。
    *   摄像头模组 (OV5640) 与 屏幕 (LCD) 连接正确。
    *   串口工具 (如 Serial Monitor) 连接至 Debug UART (波特率 921600，App Board 为 460800)。
2.  **启动 OpenOCD**：
    在单独的终端窗口中运行：
    ```bash
    openocd -f s300_openocd.cfg
    ```

## 3. 多模型检测框架 (MM_Test_Demo)

`MM_Test_Demo` 是一个通用测试框架，通过加载不同的 AI 模型固件，实现不同类型的目标检测并在屏幕上画框。

### 3.1 人脸检测 (Face Detection)
最基础的人脸检测，框出人脸位置。

**运行命令：**
```bash
ninja -C build dbg_mm_test_face-detection
```

### 3.2 人形检测 (Human Detection)
检测上半身或全身人形。

**运行命令：**
```bash
ninja -C build dbg_mm_test_human-detection
```

### 3.3 手势识别 (Hand Gesture)
检测手部位置并识别手势（如手掌、拳头等）。

**运行命令：**
```bash
ninja -C build dbg_mm_test_hand-gesture
```

*(注意：切换模型时，无需重新编译 M4 代码，只需运行对应的 `dbg_` 目标，GDB 会自动替换加载的 AI 固件。)*

---

## 4. 人脸识别系统 (Face Recognition)

该 Demo 演示端到端的人脸录入与识别流程。需配合 PC 端 Python 上位机使用。

*   **功能**：PC 端采集人脸特征 -> 传输至 S300 -> S300 实时比对并显示姓名。
*   **固件路径**：`Projects/Demo/Face_Recognition_Demo`
*   **上位机路径**：`Projects/Demo/Face_Recognition_Demo/host_app`

### 4.1 启动设备端
运行以下命令加载固件与人脸识别模型：

```bash
ninja -C build dbg_face_recognition_demo_model
```
设备启动后，屏幕应显示摄像头画面，串口打印 `[FACE] Init OK`。

### 4.2 启动上位机 (PC)
上位机使用 Python 编写，建议使用 `uv` 管理依赖。

```bash
# 1. 进入上位机目录
cd Projects/Demo/Face_Recognition_Demo/host_app

# 2. 安装依赖
uv sync

# 3. 运行上位机 (修改 COM 口为实际端口)
uv run app.py --port COMx
```

### 4.3 操作流程
1.  **注册人脸**：在上位机界面输入姓名，点击“注册”。正对摄像头，上位机会自动采集特征并下发到 S300。
2.  **实时识别**：注册完成后，S300 开发板屏幕上会出现识别框，并显示录入的姓名。

---

## 5. 互动眼球演示 (Display_Demo)

结合了 LVGL UI 与人脸检测的趣味 Demo。眼球会跟随人脸移动，并在长时间无人脸时自动闭合。

**运行命令：**
```bash
ninja -C build dbg_display_face-detection
```

**特性：**
*   **跟随**：眼球注视检测到的人脸中心。
*   **交互**：人脸消失/出现会触发 UI 状态机的切换（Active/Idle）。
*   **LVGL**：展示了高性能的局部刷新与 DMA 传输能力。
