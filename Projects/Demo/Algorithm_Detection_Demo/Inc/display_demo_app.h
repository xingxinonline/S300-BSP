#pragma once
/*
 * 模块：display_demo_app（应用编排层）
 * 作用：统一编排“摄像头预上电 → 视频链路 → 邮箱握手 → LVGL/显示绑定 → Eyes UI → 人脸追踪”的初始化，
 *       并在主循环中提供一次调用的轮询入口，保持 main.c 精简。
 * 输入：
 *   - get_millis：获取毫秒节拍（1ms）的函数指针；用于人脸追踪时序与限速计算。
 * 输出：无显式返回；模块内部通过打印与 UI 表现反馈状态。
 * 依赖：
 *   - 低层：RCC/GPIO/I2CSoft、MAILBOX、VIDEO(显示子系统)
 *   - 中层：LVGL + ui_display
 *   - 上层：eyes（眼睛UI）、face_tracker（人脸跟踪->眼睛控制）
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化显示 Demo（摄像头探测、视频子系统、邮箱、LVGL+UI、eyes、人脸追踪）。 */
void display_demo_app_init(uint32_t (*get_millis)(void));

/* 主循环中的周期轮询（人脸追踪+LVGL定时器处理）。 */
void display_demo_app_tick(void);

#ifdef __cplusplus
}
#endif
