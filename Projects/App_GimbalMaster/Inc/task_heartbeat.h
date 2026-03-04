#ifndef TASK_HEARTBEAT_H
#define TASK_HEARTBEAT_H

/**
 * @file task_heartbeat.h
 * @brief 心跳任务 (LED 指示 + 周期日志)
 *
 * 职责: WS2812 LED 动画 + 心跳日志输出
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief 初始化心跳模块 (LED 硬件)
 */
void task_heartbeat_init(void);

/**
 * @brief 启动心跳任务
 * @return 0 成功, <0 失败
 */
int task_heartbeat_start(void);

/**
 * @brief 通知一次拍照/录像触发事件（瞬时闪光提示）
 */
void task_heartbeat_notify_photo_event(void);

/**
 * @brief 设置录像模式状态灯（红色心跳）
 * @param enable true=进入录像模式, false=退出录像模式
 */
void task_heartbeat_set_recording(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* TASK_HEARTBEAT_H */
