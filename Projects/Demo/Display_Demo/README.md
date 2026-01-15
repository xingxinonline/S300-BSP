# Display_Demo：LVGL v9.4 + 互动眼球 + 人脸追踪

本示例演示 S300 平台的多媒体与显示功能，集成 **LVGL v9.4**、**DMA 加速显示** 与 **AI 人脸追踪**。

**核心特性：**
1.  **显示框架**：
    -   **双硬件 Buffer**（Front/Back）+ **DMA LLI**（整帧基线复制）+ **DMA Scatter**（局部脏区搬运）。
    -   **PARTIAL 渲染模式**：使用小缓冲区（`DRAWBUF_LINES=30`）运行 LVGL。
    -   支持帧率与 Flush 次数统计。
2.  **互动演示**：
    -   **人脸追踪**：通过 Mailbox 获取 AI 算力子系统的人脸坐标。
    -   **动态眼球**：Eyes UI 根据人脸位置移动，包含眨眼动画与空闲回中逻辑。
    -   **实时刷新**：人脸状态变化时主动触发刷新，提升响应速度。

## 目录结构

- `src/main.c`: 系统初始化与主循环调度
- `src/display_demo_app.c`: 应用编排（连接 Video、Face Tracker、Eyes）
- `src/ui_display.c`: 显示驱动适配（DMA LLI/Scatter 逻辑核心）
- `src/eyes.c`: 眼球 UI 逻辑、动画与平滑算法
- `src/face_tracker.c`: 接收 AI 子系统人脸坐标并去抖、标准化
- `CMakeLists.txt`: 构建脚本，自动拉取 LVGL v9.4

## 编译与运行

请在 **S300-BSP 仓库根目录** 下执行以下命令：

1.  **配置工程**：
    ```bash
    cmake -B build -G Ninja
    ```
    *注意：首次运行会自动下载 LVGL 源码，需保持网络连接或配置本地代理。*

2.  **编译 Demo**：
    ```bash
    ninja -C build s300_display_demo
    ```

3.  **烧录/调试**：
    -   如果你使用 VS Code，直接运行 `Build` 任务，然后使用 `Cortex-Debug` 启动调试。
    -   或者手动使用 GDB：
        ```bash
        # 加载 AI 固件与 Demo 固件
        ninja -C build dbg_display_face-detection
        ```

## 架构与配置

### 1. 显示链路优化
- **DMA LLI 帧首复制**：每帧开始时，利用 DMA 链表传输（LLI）瞬间将上一帧内容（Present Buffer）复制到当前绘制帧（Back Buffer），使得本帧只需绘制“变化区域”，未变区域保持原样（避免双缓冲常见的清屏/闪烁问题）。
- **DMA Scatter 局部搬运**：LVGL 渲染出的不连续小块（px_map），通过 DMA 目的地址散射（Destination Scatter）直接拼接到 Back Buffer 的正确位置，极大降低 CPU 搬运负载。

### 2. 宏配置参数
可在 `CMakeLists.txt` 或 `Inc/eyes.h` / `Inc/ui_display.h` 中调整：

| 宏名称                       | 默认值   | 说明                                             |
| :--------------------------- | :------- | :----------------------------------------------- |
| **UI_STAT_OVERLAY**          | 1        | 开启左上角 FPS/CPU/内存 统计悬浮窗               |
| **UI_LOG_LEVEL**             | 1 (WARN) | 日志等级 (0=ERR, 1=WARN, 2=INFO...)              |
| **DRAWBUF_LINES**            | 30       | LVGL 渲染缓冲区行数，越小越省内存但 Flush 越频繁 |
| **EYE_PER_PX_MS**            | 1.5      | 眼球移动速度（毫秒/像素），越大越慢              |
| **EYE_SMOOTH_NUM**           | 10       | 运动平滑系数分子（越大越平滑）                   |
| **FACE_PRESENCE_CONFIRM_MS** | 200      | 人脸持续多久才确认“出现”（防抖动）               |

## 常见问题

- **眼球不动/无反应**：
    - 检查摄像头（OV5640）是否连接牢固。
    - 确认是否已加载 AI 固件（人脸算法运行在 AI 算力系统上）。
    - 检查串口日志 `[FACE]` 相关输出。
- **画面撕裂**：
    - 通常由 DMA 带宽竞争引起，本 Demo 采用了 LLI 串行化规避，若修改了时序请注意 DMA 通道占用。
- **编译时 LVGL 下载失败**：
    - 请参考 `CMakeLists.txt` 中的 `LVGL_LOCAL_PATH` 选项，手动指定本地 LVGL 路径。

---
*基于 LVGL v9.4 构建，适配 PiMCHIP S300。*
