#pragma once
/*
 * 模块：ui_display（LVGL 显示绑定）
 * 作用：
 *  - 初始化 LVGL 并将其与硬件帧缓冲绑定（双缓冲，整帧渲染 FULL 模式）；
 *  - 提供屏幕背景色设置；
 * 设计要点：
 *  - 使用 DSP 显示子系统的读帧地址空间（DISP_RFRAME*）作为 LVGL render buffer；
 *  - 刷新回调中通过寄存器“敲门”方式通知显示侧切换，并等待完成（简化同步）；
 *  - 本模块不创建 UI 控件，仅提供显示层抽象，便于上层（Eyes/APP）使用。
 */
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 LVGL 并绑定硬件显示（返回创建的 lv_display_t*）。 */
lv_display_t * ui_display_init(void);

/* 设置活动屏幕背景色（24位RGB，例如 0xRRGGBB）。 */
void ui_display_set_bg_color(uint32_t rgb24);

/* 主动请求一次轻量刷新：
 * - 优先无效化顶层统计标签（若存在），触发最小矩形刷新；
 * - 否则无效化当前屏幕，实现兜底刷新。
 * 适用于外部事件（如人脸“出现/消失”）需要立即呈现的场景。
 */
void ui_request_refresh(void);

#ifdef __cplusplus
}
#endif
