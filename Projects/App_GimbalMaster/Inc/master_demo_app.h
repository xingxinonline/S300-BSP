#ifndef MASTER_DEMO_APP_H
#define MASTER_DEMO_APP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int master_demo_app_init(uint32_t (*get_millis_fn)(void));
void master_demo_app_tick(void);
bool master_demo_app_is_subboard_running(void);
uint8_t master_demo_app_get_subboard_state(void);

#ifdef __cplusplus
}
#endif

#endif /* MASTER_DEMO_APP_H */