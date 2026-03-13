#ifndef APP_GIMBAL_MASTER_FILL_LIGHT_H
#define APP_GIMBAL_MASTER_FILL_LIGHT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int app_fill_light_init(void);
int app_fill_light_set_enabled(bool enabled);
bool app_fill_light_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif