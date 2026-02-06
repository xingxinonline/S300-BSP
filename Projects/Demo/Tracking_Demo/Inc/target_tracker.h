/**
 * @file    target_tracker.h
 * @brief   目标跟踪模块 (PID控制器 + 双缓冲显示)
 * 
 * 支持特性：
 *   - track_id 跨帧关联
 *   - 卡尔曼滤波置信度 (kf_confidence)
 *   - 速度箭头渲染
 *   - 双缓冲无撕裂显示
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

/** @brief 跟踪目标信息（从 DSP 检测结果解析）*/
typedef struct {
    int32_t x1, y1, x2, y2;     /**< 目标框坐标 */
    float score;                 /**< 置信度 */
    bool valid;                  /**< 是否有效 */
    uint32_t timestamp;          /**< 时间戳 */
    
    /* 新增：协议 v2.x 字段 */
    uint8_t track_id;            /**< 跟踪ID（用于跨帧关联，0表示未分配）*/
    int8_t  vx;                  /**< X方向速度 (像素/帧, 卡尔曼输出) */
    int8_t  vy;                  /**< Y方向速度 (像素/帧, 卡尔曼输出) */
    uint8_t speed;               /**< 速度大小 [0-255]，用于箭头渲染 */
    uint8_t kf_confidence;       /**< 卡尔曼滤波置信度 (0-100) */
    uint8_t edge_flags;          /**< 边缘标记: bit0=左, bit1=右, bit2=上, bit3=下 */
    bool    selected;            /**< 是否为选中目标 */
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

/**
 * @brief  获取当前跟踪的 track_id
 * @return 当前跟踪的 track_id，0表示无跟踪
 */
uint8_t tracker_get_current_track_id(void);

/**
 * @brief  获取最后一次跟踪位置
 * @param  out_cx 输出 X 坐标
 * @param  out_cy 输出 Y 坐标
 */
void tracker_get_last_position(int16_t *out_cx, int16_t *out_cy);

/**
 * @brief  获取丢失状态和丢失位置
 * @param  out_cx 输出丢失时的 X 坐标 (可为 NULL)
 * @param  out_cy 输出丢失时的 Y 坐标 (可为 NULL)
 * @return true=当前处于 LOST 状态
 */
bool tracker_is_lost(int16_t *out_cx, int16_t *out_cy);

/**
 * @brief  获取 LOST 状态的持续时间
 * @return LOST 状态持续的毫秒数，非 LOST 状态返回 0
 */
uint32_t tracker_get_lost_duration_ms(void);

/**
 * @brief  重置跟踪器（清除当前跟踪的 track_id）
 */
void tracker_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* TARGET_TRACKER_H */
