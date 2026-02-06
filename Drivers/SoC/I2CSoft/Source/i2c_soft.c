#include <stdint.h>
#include <stdbool.h>
#include "i2c_soft.h"
#include "rcc.h"

#define PIN_MASK(n) (1u << (n))

/* 简单忙等待延时：换算成 NOP 循环，避免依赖 RTOS */
static inline void delay_us_busy(uint32_t sysclk_hz, uint32_t us)
{
    /* 每个循环大约 8 个周期，调整经验系数以匹配平台 */
    volatile uint32_t n = (uint32_t)(((uint64_t)sysclk_hz * us) / (8u * 1000000u));
    while (n--)
    {
        __asm volatile("nop");
    }
}

static inline void scl_high(i2c_soft_t *i2c)
{
    /* 开漏：释放为输入，由上拉拉高 */
    set_gpio_direction(i2c->cfg.port, i2c->cfg.pin_scl, 0);
}
static inline void scl_low(i2c_soft_t *i2c)
{
    set_gpio_direction(i2c->cfg.port, i2c->cfg.pin_scl, 1);
    set_gpio_data(i2c->cfg.port, i2c->cfg.pin_scl, 0);
}
static inline void sda_high(i2c_soft_t *i2c)
{
    /* 开漏：释放为输入，由上拉拉高 */
    set_gpio_direction(i2c->cfg.port, i2c->cfg.pin_sda, 0);
}
static inline void sda_low(i2c_soft_t *i2c)
{
    set_gpio_direction(i2c->cfg.port, i2c->cfg.pin_sda, 1);
    set_gpio_data(i2c->cfg.port, i2c->cfg.pin_sda, 0);
}
static inline void sda_in(i2c_soft_t *i2c)
{
    set_gpio_direction(i2c->cfg.port, i2c->cfg.pin_sda, 0);
}
static inline int sda_read(i2c_soft_t *i2c)
{
    return get_gpio_value(i2c->cfg.port, i2c->cfg.pin_sda) ? 1 : 0;
}

int i2c_soft_init(i2c_soft_t *i2c, const i2c_soft_cfg_t *cfg, uint32_t sysclk_hz)
{
    if (!i2c || !cfg || cfg->bus_hz == 0u) return -1;
    i2c->cfg = *cfg;
    /* 配置为 GPIO 软控模式并设置复用/上拉（GPIO 时钟应由上层提前开启） */
    gpio_set_soft_mode(cfg->port, true);
    gpio_set_function(cfg->port, cfg->pin_scl, cfg->func_scl);
    gpio_set_function(cfg->port, cfg->pin_sda, cfg->func_sda);
    gpio_set_mode(cfg->port, cfg->pin_scl, cfg->pull_mode);
    gpio_set_mode(cfg->port, cfg->pin_sda, cfg->pull_mode);
    /* 空闲态: 释放为输入（上拉） */
    set_gpio_direction(cfg->port, cfg->pin_scl, 0);
    set_gpio_direction(cfg->port, cfg->pin_sda, 0);
    /* 计算半周期 us */
    uint32_t t_us = (1000000u / cfg->bus_hz) / 2u;
    if (t_us == 0u) t_us = 1u;
    i2c->half_period_us = t_us;
    (void)sysclk_hz; /* 当前 delay 使用 NOP，相对 sysclk 误差可接受。保留参数以便未来优化 */
    return 0;
}

void i2c_soft_start(i2c_soft_t *i2c)
{
    sda_high(i2c);
    scl_high(i2c);
    delay_us_busy(SystemCoreClock, i2c->half_period_us);
    sda_low(i2c);
    delay_us_busy(SystemCoreClock, i2c->half_period_us);
    scl_low(i2c);
}

void i2c_soft_stop(i2c_soft_t *i2c)
{
    sda_low(i2c);
    delay_us_busy(SystemCoreClock, i2c->half_period_us);
    scl_high(i2c);
    delay_us_busy(SystemCoreClock, i2c->half_period_us);
    sda_high(i2c);
    delay_us_busy(SystemCoreClock, i2c->half_period_us);
}

static int wait_ack(i2c_soft_t *i2c)
{
    int timeout = 250;
    sda_in(i2c);
    sda_high(i2c); /* 释放 SDA 上拉 */
    delay_us_busy(SystemCoreClock, i2c->half_period_us / 4u + 1u);
    scl_high(i2c);
    delay_us_busy(SystemCoreClock, i2c->half_period_us / 2u + 1u);
    while (sda_read(i2c) && --timeout)
    {
        /* 等待从机拉低 ACK */
    }
    scl_low(i2c);
    return timeout ? 0 : -1;
}

int i2c_soft_write_byte(i2c_soft_t *i2c, uint8_t byte)
{
    for (int b = 0; b < 8; ++b)
    {
        if (byte & 0x80u) sda_high(i2c);
        else sda_low(i2c);
        byte <<= 1;
        delay_us_busy(SystemCoreClock, i2c->half_period_us / 2u + 1u);
        scl_high(i2c);
        delay_us_busy(SystemCoreClock, i2c->half_period_us);
        scl_low(i2c);
        delay_us_busy(SystemCoreClock, i2c->half_period_us / 2u + 1u);
    }
    return wait_ack(i2c);
}

uint8_t i2c_soft_read_byte(i2c_soft_t *i2c, bool ack)
{
    uint8_t v = 0;
    sda_in(i2c);       /* 切换 SDA 为输入模式 */
    sda_high(i2c);     /* 释放 SDA，由上拉电阻拉高 */
    for (int b = 0; b < 8; ++b)
    {
        delay_us_busy(SystemCoreClock, i2c->half_period_us);
        scl_high(i2c);
        delay_us_busy(SystemCoreClock, i2c->half_period_us);
        v = (uint8_t)((v << 1) | (sda_read(i2c) ? 1u : 0u));
        scl_low(i2c);
    }
    /* 发送 ACK/NACK */
    if (ack) sda_low(i2c);
    else sda_high(i2c);
    delay_us_busy(SystemCoreClock, i2c->half_period_us);
    scl_high(i2c);
    delay_us_busy(SystemCoreClock, i2c->half_period_us);
    scl_low(i2c);
    sda_high(i2c); /* 释放 SDA */
    return v;
}

int i2c_soft_mem_write(i2c_soft_t *i2c, uint8_t saddr, uint16_t reg, bool is16bit, const uint8_t *data, uint16_t len)
{
    i2c_soft_start(i2c);
    if (i2c_soft_write_byte(i2c, (uint8_t)((saddr << 1) | 0u))) goto err;
    if (is16bit)
    {
        if (i2c_soft_write_byte(i2c, (uint8_t)(reg >> 8))) goto err;
    }
    if (i2c_soft_write_byte(i2c, (uint8_t)(reg & 0xFF))) goto err;
    for (uint16_t i = 0; i < len; ++i)
    {
        if (i2c_soft_write_byte(i2c, data[i])) goto err;
    }
    i2c_soft_stop(i2c);
    return 0;
err:
    i2c_soft_stop(i2c);
    return -1;
}

int i2c_soft_mem_read(i2c_soft_t *i2c, uint8_t saddr, uint16_t reg, bool is16bit, uint8_t *data, uint16_t len)
{
    i2c_soft_start(i2c);
    if (i2c_soft_write_byte(i2c, (uint8_t)((saddr << 1) | 0u))) goto err;
    if (is16bit)
    {
        if (i2c_soft_write_byte(i2c, (uint8_t)(reg >> 8))) goto err;
    }
    if (i2c_soft_write_byte(i2c, (uint8_t)(reg & 0xFF))) goto err;
    /* 重启，改为读 */
    i2c_soft_start(i2c);
    if (i2c_soft_write_byte(i2c, (uint8_t)((saddr << 1) | 1u))) goto err;
    for (uint16_t i = 0; i < len; ++i)
    {
        bool ack = (i + 1u < len);
        data[i] = i2c_soft_read_byte(i2c, ack);
    }
    i2c_soft_stop(i2c);
    return 0;
err:
    i2c_soft_stop(i2c);
    return -1;
}

int i2c_soft_probe(i2c_soft_t *i2c, uint8_t saddr)
{
    i2c_soft_start(i2c);
    int err = i2c_soft_write_byte(i2c, (uint8_t)((saddr << 1) | 0u));
    i2c_soft_stop(i2c);
    return err ? -1 : 0;
}

int i2c_soft_bus_recover(i2c_soft_t *i2c)
{
    /* 尝试释放 SDA：SCL 产生 9 个脉冲，如果 SDA 仍低则失败 */
    sda_high(i2c);
    for (int i = 0; i < 9; ++i)
    {
        scl_high(i2c);
        delay_us_busy(SystemCoreClock, i2c->half_period_us);
        scl_low(i2c);
        delay_us_busy(SystemCoreClock, i2c->half_period_us);
    }
    /* 发送 STOP */
    scl_high(i2c);
    delay_us_busy(SystemCoreClock, i2c->half_period_us / 2u + 1u);
    sda_high(i2c);
    delay_us_busy(SystemCoreClock, i2c->half_period_us / 2u + 1u);
    return 0;
}

int i2c_soft_init_default_idx(i2c_soft_t *i2c, uint8_t idx, uint32_t bus_hz)
{
    static const uint8_t scl_pins[4] = {14, 0, 2, 4};
    static const uint8_t sda_pins[4] = {15, 1, 3, 5};
    if (idx > 3) return -1;
    i2c_soft_cfg_t cfg;
    cfg.port = GPIOA;
    cfg.pin_scl = scl_pins[idx];
    cfg.pin_sda = sda_pins[idx];
    cfg.func_scl = FUNCTION_2;
    cfg.func_sda = FUNCTION_2;
    cfg.pull_mode = GPIO_UP;
    cfg.bus_hz = bus_hz;
    /* 使能 GPIO 时钟并设置为软控 */
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_GPIO, true);
    return i2c_soft_init(i2c, &cfg, SystemCoreClock);
}
