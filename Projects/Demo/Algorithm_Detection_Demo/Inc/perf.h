#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Simple CPU usage metering without DWT:
 * - SysTick increments total ticks every 1 ms
 * - When main loop is in idle spin, set g_cpu_in_idle=1 so SysTick also increments idle ticks
 * - CPU usage = (delta_total - delta_idle) / delta_total
 */
extern volatile uint32_t g_cpu_total_ticks;
extern volatile uint32_t g_cpu_idle_ticks;
extern volatile uint8_t  g_cpu_in_idle;

#ifdef __cplusplus
}
#endif
