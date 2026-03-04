#ifndef TASK_STATE_H
#define TASK_STATE_H

/**
 * @file task_state.h
 * @brief 状态机服务任务
 *
 * 设计原则:
 * - 单一职责: 仅负责消费事件队列、驱动状态机、输出日志
 * - 低耦合: 与生产者(命令/KWS/手势)通过事件队列解耦
 * - 可扩展: 提供状态变化回调注册接口
 */

#include "track_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 状态变化回调类型 */
typedef void (*StateChangeCallback_t)(const TrackTransition_t *transition);

/* 拍照请求回调类型 */
typedef void (*PhotoRequestCallback_t)(TrackState_t current_state);

/**
 * @brief 初始化状态机服务
 * @return 0 成功, <0 失败
 */
int task_state_init(void);

/**
 * @brief 启动状态机服务任务
 * @return 0 成功, <0 失败
 */
int task_state_start(void);

/**
 * @brief 注册状态变化回调 (可选)
 * @param cb 回调函数
 */
void task_state_set_change_callback(StateChangeCallback_t cb);

/**
 * @brief 注册拍照请求回调 (可选)
 * @param cb 回调函数
 */
void task_state_set_photo_callback(PhotoRequestCallback_t cb);

#ifdef __cplusplus
}
#endif

#endif /* TASK_STATE_H */
