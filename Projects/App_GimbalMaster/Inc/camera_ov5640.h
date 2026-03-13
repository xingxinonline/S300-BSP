#ifndef APP_GIMBAL_MASTER_CAMERA_OV5640_H
#define APP_GIMBAL_MASTER_CAMERA_OV5640_H

#include <stdbool.h>
#include <stdint.h>

#include "i2c_soft.h"

#ifdef __cplusplus
extern "C" {
#endif

int camera_ov5640_preinit(void);
bool camera_ov5640_get_context(i2c_soft_t **i2c, uint8_t *saddr);

#ifdef __cplusplus
}
#endif

#endif