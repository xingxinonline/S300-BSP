#pragma once
/*
 * 模块：face_tracker（人脸跟踪 → 眼睛运动）
 * 作用：
 *  - 从 MAILBOX 读取 DSP 侧写入的人脸框偏移（相对共享基地址）；
 *  - 解析面框中心，按 Eyes 的有效范围映射到屏幕中点坐标；
 *  - 应用 EMA 平滑、最小移动阈值与速率限制，并驱动 eyes 移动；
 *  - 在无脸超时后切换到眨眼闭合态（动画由 eyes 模块提供）。
 * 依赖：eyes.h、mailbox.h、video.h（获取屏幕宽高）。
 */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：传入毫秒节拍提供者（1ms），用于时序与限速计算。 */
void face_tracker_init(uint32_t (*get_millis_fn)(void));

/* 轮询：读取邮箱、做坐标映射与滤波限速，按需触发眨眼或移动眼睛。 */
void face_tracker_poll(void);

#ifdef __cplusplus
}
#endif
