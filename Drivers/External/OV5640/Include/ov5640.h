#ifndef S300_BSP_OV5640_H
#define S300_BSP_OV5640_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "s300.h"
#include "gpio.h"
#include "i2c_soft.h"

#define OV5640_I2C_ADDR 0x3C /* 7bit */

typedef enum
{
    OV5640_FMT_YUV422_YUYV = 0x30,
    OV5640_FMT_YUV422_YVYU = 0x31,
    OV5640_FMT_YUV422_UYVY = 0x32,
    OV5640_FMT_YUV422_VYUY = 0x33,
    OV5640_FMT_RGB565_R5G3_G3B5 = 0x61,
} ov5640_format_t;

/* 初始化：使用软 I2C 将传感器配置到指定输出格式。返回 0 表示成功 */
int ov5640_init(i2c_soft_t *i2c, uint8_t saddr, ov5640_format_t fmt);

/* 测试图：颜色条/色块 */
int ov5640_set_color_bar(i2c_soft_t *i2c, uint8_t saddr, bool en);
int ov5640_set_color_square(i2c_soft_t *i2c, uint8_t saddr, bool en);

/* 补光灯/闪光灯控制：开启时写 0x3016/0x301C/0x3019 = 0x02，关闭时写 0x3019 = 0x00 */
int ov5640_set_light(i2c_soft_t *i2c, uint8_t saddr, bool en);

/* 硬件复位与上电时序控制 */
void ov5640_hard_init(void);

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_OV5640_H */
