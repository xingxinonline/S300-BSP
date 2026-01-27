/**
 * @file    aiming_ctrl.h
 * @brief   视觉指向控制器 (摄像头固定，云台指向目标)
 * 
 * @details 与追踪模式不同，这里摄像头是固定的，云台需要指向
 *          摄像头看到的目标位置。坐标映射方向与追踪模式相反。
 */
#ifndef AIMING_CTRL_H
#define AIMING_CTRL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 配置
 *===========================================================================*/

/** @brief 控制周期 (ms) */
#define AIMING_CONTROL_PERIOD_MS    50

/** @brief 目标丢失超时 (ms) */
#define AIMING_TARGET_TIMEOUT_MS    500

/** @brief 默认死区 (像素) */
#define AIMING_DEFAULT_DEADZONE     10

/*===========================================================================
 * 数据结构
 *===========================================================================*/

/**
 * @brief 目标信息
 */
typedef struct {
    int32_t x1, y1;         /**< 左上角 */
    int32_t x2, y2;         /**< 右下角 */
    float   score;          /**< 置信度 */
    bool    valid;          /**< 是否有效 */
    uint32_t timestamp;     /**< 时间戳 */
} aiming_target_t;

/*===========================================================================
 * API
 *===========================================================================*/

/**
 * @brief  初始化指向控制器
 * @param  get_millis  时间戳获取函数
 * @param  img_width   图像宽度
 * @param  img_height  图像高度
 */
void aiming_init(uint32_t (*get_millis)(void), int img_width, int img_height);

/**
 * @brief  更新目标位置
 * @param  target  目标信息
 */
void aiming_update_target(const aiming_target_t *target);

/**
 * @brief  轮询控制 (在主循环中调用)
 */
void aiming_poll(void);

/**
 * @brief  使能/禁用指向控制
 * @param  enable  是否使能
 */
void aiming_set_enable(bool enable);

/**
 * @brief  是否正在指向目标
 * @return true=正在指向
 */
bool aiming_is_active(void);

/**
 * @brief  设置 PID 参数
 * @param  kp  比例系数
 * @param  ki  积分系数
 * @param  kd  微分系数
 */
void aiming_set_pid(float kp, float ki, float kd);

/**
 * @brief  设置死区
 * @param  pixels  死区大小 (像素)
 */
void aiming_set_deadzone(int pixels);

/**
 * @brief  设置视场角映射参数
 * @param  fov_x  水平视场角 (度)
 * @param  fov_y  垂直视场角 (度)
 * @note   用于将像素偏移转换为角度偏移
 */
void aiming_set_fov(float fov_x, float fov_y);

#ifdef __cplusplus
}
#endif

#endif /* AIMING_CTRL_H */
