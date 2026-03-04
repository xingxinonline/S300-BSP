#ifndef TASK_COMMAND_H
#define TASK_COMMAND_H

/**
 * @file task_command.h
 * @brief 串口命令任务
 *
 * 职责: 解析串口输入，通过事件队列发送命令
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动命令任务
 * @return 0 成功, <0 失败
 */
int task_command_start(void);

#ifdef __cplusplus
}
#endif

#endif /* TASK_COMMAND_H */
