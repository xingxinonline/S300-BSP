# Display_Demo + LVGL v9.4（双缓冲 + 局部刷新 + DMA LLI/Scatter）

本 demo 演示在 S300 平台上将 LVGL v9.4 与硬件显示深度结合，实现：

- 双硬件读帧 ping-pong（front/back）
- 帧首整帧基线复制（DMA LLI：front → back）
- 帧内局部刷新（DMA 目的散射，将 LVGL 的 px_map 矩形搬运至 back）
- 帧尾呈现（present back 为新的 front）
- 1Hz 统计叠加（fps/flush/cpu/mem）+ 轻量保活刷新

适合在内存受限场景使用小 draw buffer 进行 PARTIAL 渲染，同时充分利用 DMA 减少 CPU 搬运。

## 获取 LVGL 源码（离线可选）

默认通过 CMake FetchContent 在线拉取 `https://github.com/lvgl/lvgl.git` `v9.4.0` 标签。如果网络不可用，有两种离线方式：

1. 供应本地路径：
   - 将 LVGL 源码克隆到本地：
     - `git clone --depth=1 --branch v9.4.0 https://github.com/lvgl/lvgl.git /path/to/lvgl`
   - 配置时指定：`-DLVGL_LOCAL_PATH=/path/to/lvgl`

2. vendoring（把源码放到仓库目录）：
   - 将 LVGL 源码放入：`Projects/Demo/Display_Demo/third_party/lvgl/lvgl/`，该目录包含官方 `CMakeLists.txt`。

如果你选择方式 (2)，则无需设置 `LVGL_LOCAL_PATH`，CMake 会优先使用 vendor 目录。

## 配置选项

- `LV_CONF_PATH`：默认已指向 `third_party/lvgl/lv_conf.h`。
- `LVGL_LOCAL_PATH`：离线本地 LVGL 路径（可选）。

## 架构概览

渲染/显示的帧循环分为三个阶段：

1) 帧首（LV_EVENT_REFR_START）
   - 使用 DMA LLI 将“正在显示的 front 帧缓冲”整帧复制到“back 帧缓冲”，作为本帧的基线。
   - 这样本帧仅需覆盖脏矩形，未更新区域也能保持与上一帧一致，避免撕裂/不同步。

2) 帧内（多次 flush 回调）
   - LVGL PARTIAL 模式渲染出紧凑矩形 px_map。
   - 使用 DMA M2M + 目的散射（destination scatter）按行搬运到 back 对应区域；小块或 DMA 忙时退化到 CPU memcpy。
   - 根据矩形对齐情况选择 16/32-bit 传输，尽可能提高带宽。

3) 帧尾（LV_EVENT_REFR_READY）
   - “敲门”寄存器将 back 提交为新的 front（present），完成显示切换。

### DMA 通道分工

- UI_DMA_CH_LLI（例如 ch3）：帧首整帧 LLI 复制（阻塞调用，时延极短）
- UI_DMA_CH_SCATTER（例如 ch2）：帧内矩形搬运（带中断，完成后通知 lv_display_flush_ready）

注意：基线 LLI 复制与矩形搬运串行化处理，避免 DMA 通道竞争。

## LVGL 集成与关键点

- 渲染模式：`LV_DISPLAY_RENDER_MODE_PARTIAL` + 两块小 draw buffer（示例：`DRAWBUF_LINES=30`）
- 事件驱动：
  - `LV_EVENT_REFR_START`：调用 `do_fullframe_baseline_copy()`
  - `LV_EVENT_REFR_READY`：`present back` + 统计与叠加刷新
- 刷新回调：`lvgl_flush_cb()` 内调用 `start_dma_rect_copy()` 来进行 DMA 矩形搬运
- 轻量保活：默认每 200ms 仅无效化顶层小标签，触发最小区域刷新，防止“无脏区时链路停摆”

## 可视化与统计（叠加层）

- 顶层三行叠加：
  - 第1行：`fps` 与 `flush`（定宽对齐，列距稳定）
  - 第2行：`cpu=XX%  mem=YY%`（CPU 使用率 + LVGL 内存池使用率）
- 竖向显示（从上到下）：启用 `LV_USE_TRANSFORM` 后，通过旋转 90° 将叠加竖排显示。
- 1Hz 统计日志：`[UI][STAT] fps=.. flush=.. cpu=..% mem=..%`

### 开关与调节

- `UI_STAT_OVERLAY`（默认 1）：是否显示叠加
- `UI_STAT_VERTICAL`（默认 1）：叠加竖排显示
- `UI_LOG_LEVEL`（默认 WARN=1）：日志等级，0~4（ERROR~VERBOSE）
- `DRAWBUF_LINES`：调节部分刷新块高度，权衡帧率/flush 次数/内存
- `UI_KEEPALIVE_ENABLE`（默认 1）与 `UI_KEEPALIVE_MS`（默认 200ms）：轻量保活周期

### CPU 使用率

- 无 DWT 情况下，使用 SysTick 计数 total/idle ticks 的差值估算，`main.c` 中在空闲段标记 `g_cpu_in_idle`。

### LVGL 内存使用率

- 采用内置 allocator（`LV_USE_STDLIB_MALLOC=LV_STDLIB_BUILTIN`）并固定内存池地址/大小（见 `third_party/lvgl/lv_conf.h`）。
- `lv_mem_monitor()` 取得 `total/free` 并计算使用率。

## 即时刷新触发（人脸出现/消失）

- 新增接口：`ui_request_refresh()`（`ui_display.h`）
  - 优先无效化顶部叠加标签（最小矩形），否则无效化当前 screen。
  - 用于外部事件需要“立即呈现”的场景。
- 已与人脸跟踪模块对接：
  - 人脸“确认出现”时（达到 `FACE_PRESENCE_CONFIRM_MS`）→ `ui_request_refresh()`
  - 人脸“丢失进入 idle”时 → `ui_request_refresh()`
  - 文件：`Src/face_tracker.c`

## 运行

- 构建：
  - `cmake -S S300_BSP -B S300_BSP/build`
  - `cmake --build S300_BSP/build --target s300_display_demo -- -j`
- 烧录与调试可参见仓库根部文档与 `dbg_display` 目标。

### 可选调试目标（自动拉起 GDB，加载 DSP boot images）

- `ninja -C S300_BSP/build dbg_display_dsp`

## 性能与调优建议

- DMA 32-bit 传输：要求矩形 `x1` 为偶数且 `w` 为偶数，可提升带宽。
- 小块阈值：过小矩形用 CPU memcpy 反而更省（示例阈值：`pix < 256`）。
- `DRAWBUF_LINES`：增大可减少 flush 次数，但增大 draw buffer 占用；结合你的 UI 动效进行 A/B 观察 fps/flush。
- 保活周期：200ms 足以维持“有变化就刷新”的体感；如需更灵敏可适当降低，但注意 DMA/CPU 开销。

## 故障排查

- 叠加被覆盖：确保在 `lv_layer_top()` 上创建；本 demo 已在初始化阶段创建顶层标签。
- 竖排旋转无效：确认 `LV_USE_TRANSFORM=1`（`lv_conf.h`）。
- 刷新“偶发停摆”：检查是否误关 `UI_KEEPALIVE_ENABLE`，或外部事件未调用 `ui_request_refresh()`。
- 链路撕裂/脏区残留：确认帧首 LLI 基线复制正常执行；避免与矩形搬运同时占用 DMA 通道。

## 说明

- SysTick 以 1ms 调用 `lv_tick_inc(1)`。
- LVGL 使用 PARTIAL 模式 + 小 draw buffer；`flush_cb` 通过 DMA 目的散射搬运矩形，帧尾切换显示。
- 默认分辨率 128x160，可按硬件调整 `video.h` 的 `DISP_IMAGE_WIDTH/HEIGHT`。
