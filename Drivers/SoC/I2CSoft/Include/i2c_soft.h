#ifndef S300_BSP_I2C_SOFT_H
#define S300_BSP_I2C_SOFT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "s300.h"
#include "gpio.h"

/* CMSIS 风格的软件 I2C 接口，仅实现主机模式，7/16 位从设备寄存器地址可选 */

typedef struct
{
    gpio_port_t port;
    uint8_t pin_scl;
    uint8_t pin_sda;
    gpio_func_t func_scl;    /* 典型为 FUNCTION_2/3 等，对应 GPIO 软控制 */
    gpio_func_t func_sda;
    uint32_t pull_mode;      /* GPIO_UP/GPIO_DOWN */
    uint32_t bus_hz;         /* 目标 I2C 频率，如 100000 或 400000 */
} i2c_soft_cfg_t;

typedef struct
{
    i2c_soft_cfg_t cfg;
    uint32_t half_period_us; /* SCL 半周期 us */
} i2c_soft_t;

/* 初始化：配置引脚为 GPIO 软控制、上拉/下拉，计算延时参数 */
int i2c_soft_init(i2c_soft_t *i2c, const i2c_soft_cfg_t *cfg, uint32_t sysclk_hz);

/* 基本单总线操作：起始/停止/写字节/读字节（带 ACK/NACK） */
void i2c_soft_start(i2c_soft_t *i2c);
void i2c_soft_stop(i2c_soft_t *i2c);
/* 返回 0 表示收到从机 ACK，非 0 表示 NACK/超时 */
int  i2c_soft_write_byte(i2c_soft_t *i2c, uint8_t byte);
/* 参数 ack=true 表示主机在读完该字节后拉低 SDA 产生 ACK；ack=false 产生 NACK（通常用于最后一个字节） */
uint8_t i2c_soft_read_byte(i2c_soft_t *i2c, bool ack);

/* 高层 API：按常见 EEPROM 寄存器模型进行写入/读取
 * saddr: 7bit 从设备地址（不包含 R/W 位）
 * reg:   8/16bit 寄存器地址；is16bit=true 表示 16 位寄存器地址
 * data:  缓冲区
 * len:   字节数
 * 返回 0 表示成功，非 0 表示失败
 */
int i2c_soft_mem_write(i2c_soft_t *i2c, uint8_t saddr, uint16_t reg, bool is16bit, const uint8_t *data, uint16_t len);
int i2c_soft_mem_read(i2c_soft_t *i2c, uint8_t saddr, uint16_t reg, bool is16bit, uint8_t *data, uint16_t len);

/* 探测 7bit 从设备地址是否应答（写方向）。返回 0 表示 ACK，-1 表示 NACK */
int i2c_soft_probe(i2c_soft_t *i2c, uint8_t saddr);

/* I2C 总线恢复：当 SDA 被从机拉低卡住时，发 9 个 SCL 脉冲并发送 STOP 释放总线 */
int i2c_soft_bus_recover(i2c_soft_t *i2c);

/* 便捷 API：按原驱动的默认 I2C 索引与引脚映射进行初始化
 * 索引 0..3 的默认映射：
 *  - I2C0: GPIOA14(SCL), GPIOA15(SDA)
 *  - I2C1: GPIOA0(SCL),  GPIOA1(SDA)
 *  - I2C2: GPIOA2(SCL),  GPIOA3(SDA)
 *  - I2C3: GPIOA4(SCL),  GPIOA5(SDA)
 * func 默认使用 FUNCTION_2，模式使用 GPIO_UP
 */
int i2c_soft_init_default_idx(i2c_soft_t *i2c, uint8_t idx, uint32_t bus_hz);

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_I2C_SOFT_H */
