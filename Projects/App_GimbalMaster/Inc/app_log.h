#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_log_init(uint8_t uart_idx);
void app_log_putc(char c);
void app_log_puts(const char *s);
void app_log_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOG_H */
