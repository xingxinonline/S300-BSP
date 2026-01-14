#pragma once
/*
 * 模块：Eyes 眼睛UI与运动控制
 * 作用：
 *  - 创建上下两个“眼睛”（眼球+眼眶背景）的 LVGL 元素，并提供基于“屏幕中点坐标”的移动接口；
 *  - 内置范围限制（不越出眼眶、屏幕）、运动平滑、速率限制、最小移动阈值、眨眼GIF切换等；
 *  - 通过编译期宏进行参数调优（默认值见下方，可在 CMake target_compile_definitions 覆盖）。
 */
#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "video.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 可调宏（均可在编译期覆盖）：
 * - EYE_PER_PX_MS：单位像素的运动时间（ms/px），越大越慢；用于生成补间动画时长；
 * - EYE_ANIM_LINEAR：动画路径是否线性（1=线性，0=缓入缓出）；
 * - EYE_RATE_LIMIT_PX_PER_S：对命令目标进行速率限制（像素/秒），0 表示不限制；
 * - EYE_SMOOTH_NUM/DEN：指数滑动平均 EMA 的分子/分母（数值越大越平滑、响应越慢）；
 * - EYE_MIN_MOVE_PX：小于该像素的移动将被忽略（抖动抑制）；
 * - MOVE_LOG_MIN_INTERVAL_MS/INVALID_LOG_INTERVAL_MS：打印日志的最小间隔（仅影响调试输出频率）；
 * - FACE_LOST_TIMEOUT_MS：人脸丢失超时阈值，超过则切换为眨眼闭合态；
 * - IDLE_RETURN_LOG_MIN_INTERVAL_MS：空闲态日志最小间隔；
 * - EYES_DEBUG：开启后打印内部调试信息（可能影响时序与性能）。
 */
#ifndef EYE_PER_PX_MS
#define EYE_PER_PX_MS 30u
#endif
#ifndef EYE_ANIM_LINEAR
#define EYE_ANIM_LINEAR 0
#endif
#ifndef EYE_RATE_LIMIT_PX_PER_S
#define EYE_RATE_LIMIT_PX_PER_S 360
#endif
#ifndef EYE_SMOOTH_NUM
#define EYE_SMOOTH_NUM 1
#endif
#ifndef EYE_SMOOTH_DEN
#define EYE_SMOOTH_DEN 2
#endif
#ifndef EYE_MIN_MOVE_PX
#define EYE_MIN_MOVE_PX 2
#endif
#ifndef MOVE_LOG_MIN_INTERVAL_MS
#define MOVE_LOG_MIN_INTERVAL_MS 120u
#endif
#ifndef INVALID_LOG_INTERVAL_MS
#define INVALID_LOG_INTERVAL_MS 300u
#endif
#ifndef FACE_LOST_TIMEOUT_MS
#define FACE_LOST_TIMEOUT_MS 1500u
#endif
#ifndef IDLE_RETURN_LOG_MIN_INTERVAL_MS
#define IDLE_RETURN_LOG_MIN_INTERVAL_MS 1000u
#endif
#ifndef EYES_DEBUG
#define EYES_DEBUG 0
#endif

/* 资源说明（由生成代码提供，包含图像/动画）： */
extern const lv_image_dsc_t img_yanbai_rotated_cw;      /* socket bg (top) */
extern const lv_image_dsc_t img_yanbai1_rotated_cw;     /* socket bg (bottom) */
extern const lv_image_dsc_t img_yanzhu_small_rotated_cw;/* eyeball */
extern const lv_image_dsc_t biyan_final_rotated_cw_first9;  /* blink top */
extern const lv_image_dsc_t biyan1_final_rotated_cw_first9; /* blink bottom */

/* 对外 API：创建/销毁/参数/眨眼 */
void eyes_create(void);
void eyes_destroy(void);
void eyes_set_spacing(int32_t spacing);
void eyes_blink_show(void);
void eyes_blink_hide_and_restore(void);

/* 以“屏幕坐标系中的中点(mid_x,mid_y)”驱动眼睛（内部会做边界与动画处理） */
void eyes_move_to_mid_y(int32_t mid_y);
void eyes_move_to_xy(int32_t mid_x, int32_t mid_y);

/* 获取可用的中点范围（已考虑眼眶与屏幕边界） */
void eyes_mid_limits_y(int32_t *min_out, int32_t *max_out);
void eyes_mid_limits_x(int32_t *min_out, int32_t *max_out);

/* 读取最近一次下发的中点命令（返回1有效/0无效） */
int eyes_get_last_mid(int32_t *mid_x, int32_t *mid_y);

#ifdef __cplusplus
}
#endif
