/**
 * @file    gimbal_ctrl.h
 * @brief   云台控制模块
 */
#ifndef GIMBAL_CTRL_H
#define GIMBAL_CTRL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 云台参数配置
 *===========================================================================*/

/** @brief Yaw 舵机 ID (底座旋转) */
#define GIMBAL_YAW_ID           6

/** @brief Pitch 舵机 ID (俯仰) */
#define GIMBAL_PITCH_ID         4

/** @brief Yaw 范围 (度) */
#define GIMBAL_YAW_MIN          (-90.0f)
#define GIMBAL_YAW_MAX          (90.0f)

/** @brief Pitch 范围 (度) */
#define GIMBAL_PITCH_MIN        (-60.0f)
#define GIMBAL_PITCH_MAX        (-30.0f)

/*===========================================================================
 * API
 *===========================================================================*/

/**
 * @brief  初始化云台
 * @param  get_millis  毫秒时间戳获取函数 (用于延时)
 * @return 0=成功
 */
int gimbal_init(uint32_t (*get_millis)(void));

/**
 * @brief  云台归中
 */
void gimbal_center(void);

/**
 * @brief  移动云台到指定角度
 * @param  yaw_deg    Yaw 角度 (-90 ~ +90)
 * @param  pitch_deg  Pitch 角度 (-90 ~ 0)
 * @param  time_ms    运动时间 (ms)
 */
void gimbal_move(float yaw_deg, float pitch_deg, uint16_t time_ms);

/**
 * @brief  获取当前云台角度
 * @param  yaw_deg    返回 Yaw 角度
 * @param  pitch_deg  返回 Pitch 角度
 * @return 0=成功
 */
int gimbal_get_position(float *yaw_deg, float *pitch_deg);

/**
 * @brief  增量移动云台
 * @param  delta_yaw    Yaw 增量 (度)
 * @param  delta_pitch  Pitch 增量 (度)
 * @param  time_ms      运动时间 (ms)
 */
void gimbal_move_delta(float delta_yaw, float delta_pitch, uint16_t time_ms);

#ifdef __cplusplus
}
#endif

#endif /* GIMBAL_CTRL_H */
