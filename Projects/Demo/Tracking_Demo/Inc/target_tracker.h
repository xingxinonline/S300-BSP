/**
 * @file    target_tracker.h
 * @brief   目标跟踪模块 (PID控制器)
 */
#ifndef TARGET_TRACKER_H
#define TARGET_TRACKER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 跟踪目标结构
 *===========================================================================*/

typedef struct {
    int32_t x1, y1, x2, y2;     /* 目标框坐标 */
    float score;                 /* 置信度 */
    bool valid;                  /* 是否有效 */
    uint32_t timestamp;          /* 时间戳 */
} tracker_target_t;

/*===========================================================================
 * API
 *===========================================================================*/

/**
 * @brief  初始化跟踪器
 * @param  get_millis  毫秒时间戳函数
 * @param  img_width   图像宽度
 * @param  img_height  图像高度
 */
void tracker_init(uint32_t (*get_millis)(void), int img_width, int img_height);

/**
 * @brief  更新目标 (从检测结果)
 * @param  target  目标信息
 */
void tracker_update_target(const tracker_target_t *target);

/**
 * @brief  跟踪器轮询 (计算并输出控制量)
 * @note   应在主循环中周期调用
 */
void tracker_poll(void);

/**
 * @brief  设置跟踪使能
 */
void tracker_set_enable(bool enable);

/**
 * @brief  获取当前跟踪状态
 * @return true=正在跟踪目标
 */
bool tracker_is_tracking(void);

/**
 * @brief  设置 PID 参数
 */
void tracker_set_pid(float kp, float ki, float kd);

/**
 * @brief  设置死区 (像素)
 */
void tracker_set_deadzone(int pixels);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_TRACKER_H */
