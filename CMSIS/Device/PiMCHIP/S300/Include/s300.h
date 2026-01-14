#ifndef PIMCHIP_S300_H
#define PIMCHIP_S300_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "device.h"
/* Device memory map and base addresses */
#include "s300_memmap.h"

/* System Core Clock */
#define HSE_CLOCK_HZ    (24000000UL)

/* Note: UART and SysTick helper declarations removed for minimal build. */

#ifdef __cplusplus
}
#endif

#endif
