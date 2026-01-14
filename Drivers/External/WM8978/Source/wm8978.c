#include "wm8978.h"
#include <string.h>
#include <stdio.h>

static uint8_t g_wm8978_addr = WM8978_I2C_ADDR;
/* 影子寄存器：WM8978 有 0..58 等常用寄存器位，保存最近写入值用于调试打印 */
static uint16_t g_wm8978_shadow[64];

/* 与原 demo 一致的默认寄存器表（0..57），用于初始化影子缓存，便于对齐打印 */
static const uint16_t g_wm8978_default_tbl[58] = {
    0x0000,0x0000,0x0000,0x0000,0x0050,0x0000,0x0140,0x0000,
    0x0000,0x0000,0x0000,0x00FF,0x00FF,0x0000,0x0100,0x00FF,
    0x00FF,0x0000,0x012C,0x002C,0x002C,0x002C,0x002C,0x0000,
    0x0032,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
    0x0038,0x000B,0x0032,0x0000,0x0008,0x000C,0x0093,0x00E9,
    0x0000,0x0000,0x0000,0x0000,0x0003,0x0010,0x0010,0x0100,
    0x0100,0x0002,0x0001,0x0001,0x0039,0x0039,0x0039,0x0039,
    0x0001,0x0001
};
void wm8978_set_addr(uint8_t saddr)
{
    g_wm8978_addr = saddr;
}

int wm8978_reg_write(i2c_soft_t *i2c, uint8_t reg, uint16_t val)
{
    /* WM8978: 7-bit dev addr, write payload is 9-bit register (reg<<1 | val[8]) + 8-bit val[7:0] */
    uint8_t buf[2];
    buf[0] = (uint8_t)((reg << 1) | ((val >> 8) & 0x01));
    buf[1] = (uint8_t)(val & 0xFF);
    int rc = i2c_soft_mem_write(i2c, g_wm8978_addr, buf[0], false, &buf[1], 1);
    if (rc == 0 && reg < (uint16_t)(sizeof(g_wm8978_shadow)/sizeof(g_wm8978_shadow[0])))
    {
        g_wm8978_shadow[reg] = val & 0x01FFu; /* 9-bit payload per寄存器格式 */
    }
    return rc;
}

uint16_t wm8978_reg_read_cache(uint8_t reg)
{
    return (reg < WM8978_REG_COUNT) ? g_wm8978_shadow[reg] : 0;
}

int wm8978_reg_update_bits(i2c_soft_t *i2c, uint8_t reg, uint16_t mask, uint16_t val)
{
    uint16_t old = wm8978_reg_read_cache(reg);
    uint16_t newv = (old & ~mask) | (val & mask);
    if (newv == old) return 0;
    return wm8978_reg_write(i2c, reg, newv);
}

void wm8978_dump_cache(void)
{
    printf("WM8978 cache: ");
    for (int i = 0; i < 64; ++i)
    {
        if ((i % 8) == 0) printf("\n %02d:", i);
        printf(" %03x", g_wm8978_shadow[i] & 0x1FF);
    }
    printf("\n");
}

int wm8978_init(i2c_soft_t *i2c)
{
    int ret;
    /* 先把影子寄存器预置为与原 demo 表一致的默认值（仅 0..57），便于后续 RMW 与打印完全对齐 */
    for (int i = 0; i < 58; ++i) {
        g_wm8978_shadow[i] = g_wm8978_default_tbl[i] & 0x01FFu;
    }
    for (int i = 58; i < 64; ++i) {
        g_wm8978_shadow[i] = 0;
    }
    /* soft reset */
    ret = wm8978_reg_write(i2c, 0, 0);
    if (ret) return ret;
    /* 基于原始配置移植的最小可用路径 */
    wm8978_reg_write(i2c, 1, 0x001B);
    wm8978_reg_write(i2c, 2, 0x01B0);
    wm8978_reg_write(i2c, 3, 0x006C);
    wm8978_reg_write(i2c, 6, 0x0000);
    wm8978_reg_write(i2c, 7, 0x0000); /* fs 16k */
    wm8978_reg_write(i2c, 43, 1u << 4);
    wm8978_reg_write(i2c, 47, 1u << 8);
    wm8978_reg_write(i2c, 48, 1u << 8);
    wm8978_reg_write(i2c, 49, 1u << 1);
    /* 对齐原 demo：R10 置位 bit3 */
    wm8978_reg_write(i2c, 10, 1u << 3);
    wm8978_reg_write(i2c, 14, 1u << 3);
    wm8978_reg_write(i2c, 5, 0x0001);
    return 0;
}

int wm8978_dump(i2c_soft_t *i2c)
{
    (void)i2c; /* 使用影子寄存器缓存，不访问 I2C 设备 */
    printf("WM8978 register dump:\r\n");
    for (int reg = 0; reg < 58; ++reg)
    {
        uint16_t v = wm8978_reg_read_cache((uint8_t)reg) & 0x01FFu;
        printf("R%02d = 0x%04X ", reg, (unsigned)v);
    }
    printf("\r\n");
    return 0;
}

int wm8978_set_adda(i2c_soft_t *i2c, bool dac_en, bool adc_en)
{
    /* R3: DACEN[1:0], R2: ADCEN[1:0] */
    /* 与原 demo 保持一致：基于现值做 RMW，仅改动 [1:0] 使能位 */
    if (wm8978_reg_update_bits(i2c, 3, 3u << 0, dac_en ? (uint16_t)(3u << 0) : 0u)) return -1;
    if (wm8978_reg_update_bits(i2c, 2, 3u << 0, adc_en ? (uint16_t)(3u << 0) : 0u)) return -1;
    return 0;
}

int wm8978_set_linein_gain(i2c_soft_t *i2c, uint8_t gain)
{
    gain &= 0x7;
    /* 对齐原 demo：仅更新 [6:4] 位，不设置 bit8 */
    if (wm8978_reg_update_bits(i2c, 47, (uint16_t)(7u << 4), (uint16_t)(gain << 4))) return -1;
    if (wm8978_reg_update_bits(i2c, 48, (uint16_t)(7u << 4), (uint16_t)(gain << 4))) return -1;
    return 0;
}

int wm8978_set_aux_gain(i2c_soft_t *i2c, uint8_t gain)
{
    gain &= 0x7;
    /* 对齐原 demo：仅更新 [2:0] 位，不设置 bit8 */
    if (wm8978_reg_update_bits(i2c, 47, 7u, (uint16_t)gain)) return -1;
    if (wm8978_reg_update_bits(i2c, 48, 7u, (uint16_t)gain)) return -1;
    return 0;
}

int wm8978_set_input(i2c_soft_t *i2c, bool mic_en, bool linein_en, bool aux_en)
{
    /* R2[3:2] 麦克风 PGA 使能，R44[5:4]/[1:0] 麦克输入选择；按位更新，保留其余默认位 */
    if (wm8978_reg_update_bits(i2c, 2, (uint16_t)(3u << 2), mic_en ? (uint16_t)(3u << 2) : 0u)) return -1;
    if (wm8978_reg_update_bits(i2c, 44, (uint16_t)((3u << 4) | (3u << 0)),
                               mic_en ? (uint16_t)((3u << 4) | (3u << 0)) : 0u)) return -1;
    if (wm8978_set_linein_gain(i2c, linein_en ? 5 : 0)) return -1;
    if (wm8978_set_aux_gain(i2c, aux_en ? 7 : 0)) return -1;
    return 0;
}

int wm8978_set_output(i2c_soft_t *i2c, bool dac_out_en, bool bypass_en)
{
    uint16_t v = 0;
    if (dac_out_en) v |= 1u << 0;
    if (bypass_en)
    {
        v |= 1u << 1;
        v |= 5u << 2;
    }
    if (wm8978_reg_write(i2c, 50, v)) return -1;
    if (wm8978_reg_write(i2c, 51, v)) return -1;
    return 0;
}

int wm8978_set_mic_gain(i2c_soft_t *i2c, uint8_t gain)
{
    gain &= 0x3F;
    if (wm8978_reg_write(i2c, 45, gain)) return -1;
    if (wm8978_reg_write(i2c, 46, (uint16_t)(gain | (1u << 8)))) return -1;
    return 0;
}

int wm8978_i2s_cfg(i2c_soft_t *i2c, uint8_t fmt, uint8_t len)
{
    fmt &= 0x03;
    len &= 0x03;
    return wm8978_reg_write(i2c, 4, (uint16_t)((fmt << 3) | (len << 5)));
}

int wm8978_set_hp_vol(i2c_soft_t *i2c, uint8_t voll, uint8_t volr)
{
    voll &= 0x3F;
    volr &= 0x3F;
    if (voll == 0) voll |= 1u << 6;
    if (volr == 0) volr |= 1u << 6;
    if (wm8978_reg_write(i2c, 52, voll)) return -1;
    if (wm8978_reg_write(i2c, 53, (uint16_t)(volr | (1u << 8)))) return -1;
    return 0;
}

int wm8978_set_spk_vol(i2c_soft_t *i2c, uint8_t volx)
{
    volx &= 0x3F;
    if (volx == 0) volx |= 1u << 6;
    if (wm8978_reg_write(i2c, 54, volx)) return -1;
    if (wm8978_reg_write(i2c, 55, (uint16_t)(volx | (1u << 8)))) return -1;
    return 0;
}
