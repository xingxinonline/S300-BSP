/**
 * @file    ft6x36.c
 * @brief   FT6X36 Capacitive Touch Controller Driver Implementation
 */

#include "ft6x36.h"
#include "i2c_soft.h"
#include <string.h>

/*===========================================================================
 * Private Functions
 *===========================================================================*/

/**
 * @brief 读取多个寄存器
 */
static int ft6x36_read_regs(ft6x36_t *dev, uint8_t reg, uint8_t *data, uint16_t len)
{
    i2c_soft_t *i2c = (i2c_soft_t *)dev->i2c;
    return i2c_soft_mem_read(i2c, dev->i2c_addr, reg, false, data, len);
}

/**
 * @brief 写入多个寄存器
 */
static int ft6x36_write_regs(ft6x36_t *dev, uint8_t reg, const uint8_t *data, uint16_t len)
{
    i2c_soft_t *i2c = (i2c_soft_t *)dev->i2c;
    return i2c_soft_mem_write(i2c, dev->i2c_addr, reg, false, data, len);
}

/**
 * @brief 解析单个触摸点数据
 */
static void ft6x36_parse_point(const uint8_t *raw, ft6x36_point_t *point, 
                                ft6x36_t *dev)
{
    /* 原始坐标 */
    uint16_t raw_x = ((raw[0] & 0x0F) << 8) | raw[1];
    uint16_t raw_y = ((raw[2] & 0x0F) << 8) | raw[3];
    
    /* 事件类型 (XH 高 2 位) */
    point->event = (raw[0] >> 6) & 0x03;
    
    /* 权重和区域 (如果数据可用) */
    point->weight = raw[4];
    point->area = (raw[5] >> 4) & 0x0F;
    
    /* 坐标变换 */
    if (dev->swap_xy) {
        uint16_t tmp = raw_x;
        raw_x = raw_y;
        raw_y = tmp;
    }
    
    if (dev->invert_x) {
        raw_x = dev->max_x - 1 - raw_x;
    }
    
    if (dev->invert_y) {
        raw_y = dev->max_y - 1 - raw_y;
    }
    
    /* 边界检查 */
    if (raw_x >= dev->max_x) raw_x = dev->max_x - 1;
    if (raw_y >= dev->max_y) raw_y = dev->max_y - 1;
    
    point->x = raw_x;
    point->y = raw_y;
}

/*===========================================================================
 * Public API Functions
 *===========================================================================*/

int ft6x36_init(ft6x36_t *dev, void *i2c, uint8_t i2c_addr)
{
    if (!dev || !i2c) {
        return -1;
    }
    
    memset(dev, 0, sizeof(ft6x36_t));
    dev->i2c = i2c;
    dev->i2c_addr = i2c_addr;
    
    /* 默认屏幕尺寸 */
    dev->max_x = 240;
    dev->max_y = 320;
    dev->swap_xy = false;
    dev->invert_x = false;
    dev->invert_y = false;
    
    /* 验证设备存在 */
    i2c_soft_t *i2c_dev = (i2c_soft_t *)i2c;
    if (i2c_soft_probe(i2c_dev, i2c_addr) != 0) {
        return -1;
    }
    
    /* 读取芯片 ID 进行验证 */
    uint8_t chip_id = 0;
    if (ft6x36_read_reg(dev, FT6X36_REG_CIPHER, &chip_id) != 0) {
        return -1;
    }
    
    /* FT6X36 系列芯片 ID 通常为 0x36, 0x64 等 */
    /* 这里不做严格检查，因为不同型号 ID 不同 */
    
    return 0;
}

void ft6x36_config(ft6x36_t *dev, uint16_t max_x, uint16_t max_y,
                   bool swap_xy, bool invert_x, bool invert_y)
{
    if (!dev) return;
    
    dev->max_x = max_x;
    dev->max_y = max_y;
    dev->swap_xy = swap_xy;
    dev->invert_x = invert_x;
    dev->invert_y = invert_y;
}

int ft6x36_read_touch(ft6x36_t *dev, ft6x36_touch_data_t *data)
{
    if (!dev || !data) {
        return -1;
    }
    
    memset(data, 0, sizeof(ft6x36_touch_data_t));
    
    /* 只读取 TD_STATUS 寄存器 (0x02) */
    uint8_t td_status = 0;
    if (ft6x36_read_regs(dev, FT6X36_REG_TD_STATUS, &td_status, 1) != 0) {
        return -1;
    }
    
    data->touch_count = td_status & 0x0F;
    
    if (data->touch_count > 2) {
        data->touch_count = 0;  /* 无效值，视为无触摸 */
        return 0;
    }
    
    /* 如果没有触摸，直接返回 */
    if (data->touch_count == 0) {
        return 0;
    }
    
    /* 逐个读取触摸数据寄存器 (解决多字节读取问题) */
    uint8_t buf[6];
    ft6x36_read_regs(dev, FT6X36_REG_P1_XH, &buf[0], 1);     /* 0x03 */
    ft6x36_read_regs(dev, FT6X36_REG_P1_XL, &buf[1], 1);     /* 0x04 */
    ft6x36_read_regs(dev, FT6X36_REG_P1_YH, &buf[2], 1);     /* 0x05 */
    ft6x36_read_regs(dev, FT6X36_REG_P1_YL, &buf[3], 1);     /* 0x06 */
    ft6x36_read_regs(dev, FT6X36_REG_P1_WEIGHT, &buf[4], 1); /* 0x07 */
    ft6x36_read_regs(dev, FT6X36_REG_P1_MISC, &buf[5], 1);   /* 0x08 */
    
    /* 读取手势 ID */
    ft6x36_read_regs(dev, FT6X36_REG_GEST_ID, &data->gesture_id, 1);
    
    /* 解析触摸点 1 */
    ft6x36_parse_point(buf, &data->points[0], dev);
    
    /* 如果有第二个触摸点 */
    if (data->touch_count >= 2) {
        uint8_t buf2[6];
        ft6x36_read_regs(dev, FT6X36_REG_P2_XH, &buf2[0], 1);     /* 0x09 */
        ft6x36_read_regs(dev, FT6X36_REG_P2_XL, &buf2[1], 1);     /* 0x0A */
        ft6x36_read_regs(dev, FT6X36_REG_P2_YH, &buf2[2], 1);     /* 0x0B */
        ft6x36_read_regs(dev, FT6X36_REG_P2_YL, &buf2[3], 1);     /* 0x0C */
        ft6x36_read_regs(dev, FT6X36_REG_P2_WEIGHT, &buf2[4], 1); /* 0x0D */
        ft6x36_read_regs(dev, FT6X36_REG_P2_MISC, &buf2[5], 1);   /* 0x0E */
        ft6x36_parse_point(buf2, &data->points[1], dev);
    }
    
    return 0;
}

int ft6x36_get_touch_count(ft6x36_t *dev)
{
    if (!dev) {
        return -1;
    }
    
    uint8_t status = 0;
    if (ft6x36_read_reg(dev, FT6X36_REG_TD_STATUS, &status) != 0) {
        return -1;
    }
    
    uint8_t count = status & 0x0F;
    return (count > 2) ? 2 : count;
}

int ft6x36_read_device_info(ft6x36_t *dev, ft6x36_dev_info_t *info)
{
    if (!dev || !info) {
        return -1;
    }
    
    memset(info, 0, sizeof(ft6x36_dev_info_t));
    
    /* 读取各项信息 */
    if (ft6x36_read_reg(dev, FT6X36_REG_CIPHER, &info->chip_id) != 0) {
        return -1;
    }
    
    if (ft6x36_read_reg(dev, FT6X36_REG_FIRMID, &info->firmware_id) != 0) {
        return -1;
    }
    
    if (ft6x36_read_reg(dev, FT6X36_REG_FOCALTECH_ID, &info->vendor_id) != 0) {
        return -1;
    }
    
    uint8_t ver_h = 0, ver_l = 0;
    if (ft6x36_read_reg(dev, FT6X36_REG_LIB_VER_H, &ver_h) != 0) {
        return -1;
    }
    if (ft6x36_read_reg(dev, FT6X36_REG_LIB_VER_L, &ver_l) != 0) {
        return -1;
    }
    info->lib_version = ((uint16_t)ver_h << 8) | ver_l;
    
    return 0;
}

int ft6x36_write_reg(ft6x36_t *dev, uint8_t reg, uint8_t val)
{
    return ft6x36_write_regs(dev, reg, &val, 1);
}

int ft6x36_read_reg(ft6x36_t *dev, uint8_t reg, uint8_t *val)
{
    return ft6x36_read_regs(dev, reg, val, 1);
}

int ft6x36_set_threshold(ft6x36_t *dev, uint8_t threshold)
{
    return ft6x36_write_reg(dev, FT6X36_REG_TH_GROUP, threshold);
}

int ft6x36_set_interrupt_mode(ft6x36_t *dev, uint8_t mode)
{
    return ft6x36_write_reg(dev, FT6X36_REG_G_MODE, mode);
}

int ft6x36_enter_sleep(ft6x36_t *dev)
{
    return ft6x36_write_reg(dev, FT6X36_REG_PWR_MODE, FT6X36_PWR_MODE_HIBERNATE);
}
