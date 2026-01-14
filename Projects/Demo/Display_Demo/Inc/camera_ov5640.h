#pragma once
/*
 * 模块：camera_ov5640（摄像头预初始化）
 * 作用：
 *  - 通过 GPIO/RCC 控制进行 RST/PWDN 上电时序；
 *  - 使用软 I2C(I2C1) 探测并初始化 OV5640（默认 YUV422 YUYV 格式）；
 *  - 仅做基本点亮与读 ID，用于 Demo 显示链路验证。
 */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 预初始化 OV5640；成功返回 0，失败返回负值。 */
int camera_ov5640_preinit(void);

#ifdef __cplusplus
}
#endif
