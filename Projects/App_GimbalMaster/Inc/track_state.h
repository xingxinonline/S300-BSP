#ifndef TRACK_STATE_H
#define TRACK_STATE_H

/**
 * @file track_state.h
 * @brief 跟踪状态机 - 纯逻辑模块
 *
 * 设计原则:
 * - 纯逻辑: 无 RTOS/硬件/日志依赖，可独立单元测试
 * - 高内聚: 状态转换逻辑完全封装
 * - 低耦合: 通过结果结构体返回，调用者决定后续动作
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 状态枚举 */
typedef enum {
    TRACK_STATE_IDLE = 0,   /**< 待机 - 不跟踪 */
    TRACK_STATE_TRACKING,   /**< 跟踪中 - 等待检测 */
    TRACK_STATE_LOCK,       /**< 锁定 - 检测到目标 */
    TRACK_STATE_SEARCH,     /**< 搜索 - 目标丢失 */
    TRACK_STATE_COUNT       /**< 状态数量 */
} TrackState_t;

/* 事件枚举 */
typedef enum {
    TRACK_EVT_START = 0,        /**< 启动跟踪 (语音/手势) */
    TRACK_EVT_STOP,             /**< 停止跟踪 (语音/手势) */
    TRACK_EVT_TARGET_FOUND,     /**< 检测到目标 */
    TRACK_EVT_TARGET_LOST,      /**< 目标丢失 */
    TRACK_EVT_PHOTO,            /**< 拍照请求 */
    TRACK_EVT_COUNT             /**< 事件数量 */
} TrackEvent_t;

/* 状态转换结果 */
typedef struct {
    TrackState_t prev_state;    /**< 转换前状态 */
    TrackState_t next_state;    /**< 转换后状态 */
    TrackEvent_t event;         /**< 触发事件 */
    bool changed;               /**< 状态是否变化 */
    bool is_photo;              /**< 是否拍照事件 */
} TrackTransition_t;

/**
 * @brief 重置状态机到 IDLE
 */
void track_state_reset(void);

/**
 * @brief 获取当前状态
 * @return 当前状态
 */
TrackState_t track_state_get(void);

/**
 * @brief 处理事件并返回转换结果
 * @param evt 事件
 * @return 状态转换结果 (纯逻辑，无副作用)
 */
TrackTransition_t track_state_process(TrackEvent_t evt);

/**
 * @brief 状态转字符串
 */
const char *track_state_to_string(TrackState_t state);

/**
 * @brief 事件转字符串
 */
const char *track_event_to_string(TrackEvent_t evt);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_STATE_H */