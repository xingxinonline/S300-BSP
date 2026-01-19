#pragma once
/*
 * 模块：uart_cmd（调试命令轮询，可选）
 * 作用：
 *  - 非阻塞地解析简单调试命令（例如："goto <y_mid>"），便于快速验证眼睛垂直移动；
 *  - 默认未启用，上层可在 app_tick 中按需调用。
 */
#ifdef __cplusplus
extern "C" {
#endif

/* 非阻塞轮询 UART 指令，解析并调用相应控制逻辑（如 eyes_*）。 */
void uart_cmd_poll(void);

#ifdef __cplusplus
}
#endif
