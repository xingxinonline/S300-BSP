#ifndef SUBBOARD_MM_APP_H
#define SUBBOARD_MM_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int subboard_mm_app_init(uint32_t (*get_millis_fn)(void));
void subboard_mm_app_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* SUBBOARD_MM_APP_H */