/**
 * @file    camera_ov5640.h
 * @brief   OV5640 摄像头预初始化模块
 * @details 通过 GPIO/RCC 控制进行 RST/PWDN 上电时序，
 *          使用软 I2C 探测并初始化 OV5640（默认 RGB565 格式）。
 */

#ifndef CAMERA_OV5640_H
#define CAMERA_OV5640_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief  预初始化 OV5640 摄像头
 * @note   使用 board 层配置进行引脚初始化，然后通过 I2C 配置传感器
 * @return 0 成功，负值失败
 */
int camera_ov5640_preinit(void);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_OV5640_H */
