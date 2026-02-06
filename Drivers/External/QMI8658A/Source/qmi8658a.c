/**
 * @file    qmi8658a.c
 * @brief   QMI8658A 六轴IMU传感器驱动实现
 * @details 参考正点原子SDK实现，适配S300-BSP软件I2C驱动
 */

#include "qmi8658a.h"
#include <string.h>
#include <stdio.h>  /* for debug printf */

/*===========================================================================
 * 私有函数声明
 *===========================================================================*/

static void delay_ms(uint32_t ms);
static int  qmi8658a_read_reg(qmi8658a_t *dev, uint8_t reg, uint8_t *data, uint16_t len);
static int  qmi8658a_write_reg(qmi8658a_t *dev, uint8_t reg, uint8_t data);
static void qmi8658a_read_raw_data(qmi8658a_t *dev, float *acc, float *gyr);

/*===========================================================================
 * 私有函数实现
 *===========================================================================*/

/**
 * @brief 毫秒延时 (使用 SysTick)
 */
static void delay_ms(uint32_t ms)
{
    uint32_t reload = SystemCoreClock / 1000 - 1;
    if (reload > 0xFFFFFF) reload = 0xFFFFFF;
    
    SysTick->LOAD = reload;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    
    for (uint32_t i = 0; i < ms; i++) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
    }
    
    SysTick->CTRL = 0;
}

/**
 * @brief 读取寄存器
 */
static int qmi8658a_read_reg(qmi8658a_t *dev, uint8_t reg, uint8_t *data, uint16_t len)
{
    return i2c_soft_mem_read(dev->i2c, dev->addr, reg, false, data, len);
}

/**
 * @brief 写入寄存器
 */
static int qmi8658a_write_reg(qmi8658a_t *dev, uint8_t reg, uint8_t data)
{
    return i2c_soft_mem_write(dev->i2c, dev->addr, reg, false, &data, 1);
}

/**
 * @brief 读取原始传感器数据并进行校准补偿
 */
static void qmi8658a_read_raw_data(qmi8658a_t *dev, float *acc, float *gyr)
{
    uint8_t buf[12];
    int16_t raw_acc[3], raw_gyr[3];
    float acc_raw[3], gyr_raw[3];
    
    static int cali_count = 0;
    static float gyr_sum[3] = {0.0f, 0.0f, 0.0f};

    
    /* 读取加速度计和陀螺仪数据 (12字节连续读取) */
    int ret = qmi8658a_read_reg(dev, QMI8658A_REG_AX_L, buf, 12);
    
    (void)ret;  /* I2C 读取成功 */
    
    /* 解析原始数据 */
    raw_acc[0] = (int16_t)((uint16_t)(buf[1] << 8) | buf[0]);
    raw_acc[1] = (int16_t)((uint16_t)(buf[3] << 8) | buf[2]);
    raw_acc[2] = (int16_t)((uint16_t)(buf[5] << 8) | buf[4]);
    
    raw_gyr[0] = (int16_t)((uint16_t)(buf[7] << 8) | buf[6]);
    raw_gyr[1] = (int16_t)((uint16_t)(buf[9] << 8) | buf[8]);
    raw_gyr[2] = (int16_t)((uint16_t)(buf[11] << 8) | buf[10]);
    
    /* 转换为物理量: 加速度 m/s², 陀螺仪 rad/s */
    acc_raw[0] = (float)(raw_acc[0] * QMI8658A_ONE_G) / dev->ssvt_a;
    acc_raw[1] = (float)(raw_acc[1] * QMI8658A_ONE_G) / dev->ssvt_a;
    acc_raw[2] = (float)(raw_acc[2] * QMI8658A_ONE_G) / dev->ssvt_a;
    
    gyr_raw[0] = (float)(raw_gyr[0] * QMI8658A_PI) / (dev->ssvt_g * 180.0f);
    gyr_raw[1] = (float)(raw_gyr[1] * QMI8658A_PI) / (dev->ssvt_g * 180.0f);
    gyr_raw[2] = (float)(raw_gyr[2] * QMI8658A_PI) / (dev->ssvt_g * 180.0f);
    
    /* 校准处理 - 只校准陀螺仪零偏，不校准加速度计 */
    if (!dev->calibrated) {
        if (cali_count == 0) {
            memset(gyr_sum, 0, sizeof(gyr_sum));
            cali_count++;
        } else if (cali_count < QMI8658A_MAX_CALI_COUNT) {
            for (int axis = 0; axis < 3; axis++) {
                gyr_sum[axis] += gyr_raw[axis];
            }
            cali_count++;
        } else if (cali_count == QMI8658A_MAX_CALI_COUNT) {
            for (int axis = 0; axis < 3; axis++) {
                dev->offset_acc[axis] = 0.0f;  /* 不校准加速度计 */
                dev->offset_gyr[axis] = -(gyr_sum[axis] / (QMI8658A_MAX_CALI_COUNT - 1));
            }
            dev->calibrated = true;
            cali_count++;
        }
    }
    
    /* 应用偏移校准 */
    if (dev->calibrated) {
        for (int axis = 0; axis < 3; axis++) {
            acc[axis] = acc_raw[axis];  /* 加速度计不应用偏移 */
            gyr[axis] = gyr_raw[axis] + dev->offset_gyr[axis];
        }
    } else {
        /* 校准未完成，返回原始值 */
        for (int axis = 0; axis < 3; axis++) {
            acc[axis] = acc_raw[axis];
            gyr[axis] = gyr_raw[axis];
        }
    }
}

/*===========================================================================
 * 公共 API 实现
 *===========================================================================*/

int qmi8658a_init(qmi8658a_t *dev, i2c_soft_t *i2c, uint8_t addr)
{
    if (!dev || !i2c) return -1;
    
    /* 初始化句柄 */
    memset(dev, 0, sizeof(qmi8658a_t));
    dev->i2c = i2c;
    dev->addr = addr;
    dev->calibrated = false;
    
    /* 复位传感器 */
    qmi8658a_reset(dev);
    
    /* 检查芯片ID */
    if (qmi8658a_check_id(dev) != 0) {
        return -1;
    }
    
    /* 陀螺仪校准 */
    if (qmi8658a_calibration(dev) != 0) {
        return -1;
    }
    
    /* 配置传感器: I2C模式 */
    qmi8658a_write_reg(dev, QMI8658A_REG_CTRL1, 0x60);
    
    /* 关闭传感器 */
    qmi8658a_write_reg(dev, QMI8658A_REG_CTRL7, 0x00);
    
    /* 默认配置: 加速度计 ±16g, 2000Hz; 陀螺仪 ±512dps, 2000Hz */
    dev->cfg.enable_flags = QMI8658A_ACCGYR_ENABLE;
    dev->cfg.acc_range = QMI8658A_ACC_RANGE_16G;
    dev->cfg.acc_odr = QMI8658A_ACC_ODR_2000HZ;
    dev->cfg.gyr_range = QMI8658A_GYR_RANGE_512DPS;
    dev->cfg.gyr_odr = QMI8658A_GYR_ODR_2000HZ;
    
    /* 配置加速度计 */
    qmi8658a_config_acc(dev, dev->cfg.acc_range, dev->cfg.acc_odr,
                        QMI8658A_LPF_ENABLE, QMI8658A_ST_ENABLE);
    
    /* 配置陀螺仪 */
    qmi8658a_config_gyr(dev, dev->cfg.gyr_range, dev->cfg.gyr_odr,
                        QMI8658A_LPF_ENABLE, QMI8658A_ST_ENABLE);
    
    /* 使能传感器 */
    qmi8658a_enable_sensors(dev, dev->cfg.enable_flags);
    
    return 0;
}

void qmi8658a_reset(qmi8658a_t *dev)
{
    qmi8658a_write_reg(dev, QMI8658A_REG_RESET, 0xB0);
    delay_ms(100);
    qmi8658a_write_reg(dev, QMI8658A_REG_RESET, 0x00);
    delay_ms(5);
}

int qmi8658a_check_id(qmi8658a_t *dev)
{
    uint8_t chip_id = 0;
    uint8_t revision = 0;
    int retry = 5;
    
    while (retry-- > 0) {
        qmi8658a_read_reg(dev, QMI8658A_REG_WHO_AM_I, &chip_id, 1);
        if (chip_id == QMI8658A_CHIP_ID) {
            qmi8658a_read_reg(dev, QMI8658A_REG_REVISION, &revision, 1);
            break;
        }
        delay_ms(10);
    }
    
    if (chip_id == QMI8658A_CHIP_ID && revision == QMI8658A_REVISION_ID) {
        return 0;
    }
    return -1;
}

void qmi8658a_config_acc(qmi8658a_t *dev, qmi8658a_acc_range_t range,
                         qmi8658a_acc_odr_t odr, qmi8658a_lpf_cfg_t lpf_enable,
                         qmi8658a_st_cfg_t st_enable)
{
    uint8_t ctrl_data;
    
    /* 设置灵敏度 */
    switch (range) {
        case QMI8658A_ACC_RANGE_2G:  dev->ssvt_a = (1 << 14); break;
        case QMI8658A_ACC_RANGE_4G:  dev->ssvt_a = (1 << 13); break;
        case QMI8658A_ACC_RANGE_8G:  dev->ssvt_a = (1 << 12); break;
        case QMI8658A_ACC_RANGE_16G: dev->ssvt_a = (1 << 11); break;
        default: dev->ssvt_a = (1 << 12); range = QMI8658A_ACC_RANGE_8G; break;
    }
    
    /* 配置 CTRL2 */
    ctrl_data = (uint8_t)range | (uint8_t)odr;
    if (st_enable == QMI8658A_ST_ENABLE) {
        ctrl_data |= 0x80;
    }
    qmi8658a_write_reg(dev, QMI8658A_REG_CTRL2, ctrl_data);
    
    /* 配置低通滤波器 CTRL5 */
    qmi8658a_read_reg(dev, QMI8658A_REG_CTRL5, &ctrl_data, 1);
    ctrl_data &= 0xF0;  /* 保留高4位 (陀螺仪配置) */
    if (lpf_enable == QMI8658A_LPF_ENABLE) {
        ctrl_data |= QMI8658A_ACC_LPF_MODE_3;
        ctrl_data |= 0x01;
    }
    qmi8658a_write_reg(dev, QMI8658A_REG_CTRL5, ctrl_data);
    
    dev->cfg.acc_range = range;
    dev->cfg.acc_odr = odr;
}

void qmi8658a_config_gyr(qmi8658a_t *dev, qmi8658a_gyr_range_t range,
                         qmi8658a_gyr_odr_t odr, qmi8658a_lpf_cfg_t lpf_enable,
                         qmi8658a_st_cfg_t st_enable)
{
    uint8_t ctrl_data;
    
    /* 设置灵敏度 */
    switch (range) {
        case QMI8658A_GYR_RANGE_16DPS:   dev->ssvt_g = 2048; break;
        case QMI8658A_GYR_RANGE_32DPS:   dev->ssvt_g = 1024; break;
        case QMI8658A_GYR_RANGE_64DPS:   dev->ssvt_g = 512;  break;
        case QMI8658A_GYR_RANGE_128DPS:  dev->ssvt_g = 256;  break;
        case QMI8658A_GYR_RANGE_256DPS:  dev->ssvt_g = 128;  break;
        case QMI8658A_GYR_RANGE_512DPS:  dev->ssvt_g = 64;   break;
        case QMI8658A_GYR_RANGE_1024DPS: dev->ssvt_g = 32;   break;
        case QMI8658A_GYR_RANGE_2048DPS: dev->ssvt_g = 16;   break;
        default: dev->ssvt_g = 64; range = QMI8658A_GYR_RANGE_512DPS; break;
    }
    
    /* 配置 CTRL3 */
    ctrl_data = (uint8_t)range | (uint8_t)odr;
    if (st_enable == QMI8658A_ST_ENABLE) {
        ctrl_data |= 0x80;
    }
    qmi8658a_write_reg(dev, QMI8658A_REG_CTRL3, ctrl_data);
    
    /* 配置低通滤波器 CTRL5 */
    qmi8658a_read_reg(dev, QMI8658A_REG_CTRL5, &ctrl_data, 1);
    ctrl_data &= 0x0F;  /* 保留低4位 (加速度计配置) */
    if (lpf_enable == QMI8658A_LPF_ENABLE) {
        ctrl_data |= QMI8658A_GYR_LPF_MODE_3;
        ctrl_data |= 0x10;
    }
    qmi8658a_write_reg(dev, QMI8658A_REG_CTRL5, ctrl_data);
    
    dev->cfg.gyr_range = range;
    dev->cfg.gyr_odr = odr;
}

void qmi8658a_enable_sensors(qmi8658a_t *dev, uint8_t enable_flags)
{
    qmi8658a_write_reg(dev, QMI8658A_REG_CTRL7, enable_flags);
    dev->cfg.enable_flags = enable_flags & 0x03;
    delay_ms(2);
}

int qmi8658a_read_data(qmi8658a_t *dev, qmi8658a_data_t *data)
{
    uint8_t status = 0;
    int retry = 3;
    bool data_ready = false;
    
    while (retry-- > 0) {
        qmi8658a_read_reg(dev, QMI8658A_REG_STATUS0, &status, 1);
        if (status & 0x03) {
            data_ready = true;
            break;
        }
    }
    
    if (data_ready) {
        qmi8658a_read_raw_data(dev, data->acc, data->gyr);
        
        /* 缓存数据 */
        for (int i = 0; i < 3; i++) {
            dev->last_acc[i] = data->acc[i];
            dev->last_gyr[i] = data->gyr[i];
        }
        return 0;
    } else {
        /* 返回上次数据 */
        for (int i = 0; i < 3; i++) {
            data->acc[i] = dev->last_acc[i];
            data->gyr[i] = dev->last_gyr[i];
        }
        return -1;
    }
}

float qmi8658a_read_temperature(qmi8658a_t *dev)
{
    uint8_t buf[2];
    int16_t temp_raw;
    
    qmi8658a_read_reg(dev, QMI8658A_REG_TEMP_L, buf, 2);
    temp_raw = (int16_t)((uint16_t)(buf[1] << 8) | buf[0]);
    
    return (float)temp_raw / 256.0f;
}

int qmi8658a_calibration(qmi8658a_t *dev)
{
    uint8_t status = 0;
    
    qmi8658a_write_reg(dev, QMI8658A_REG_CTRL7, 0x00);
    qmi8658a_write_reg(dev, QMI8658A_REG_CTRL9, 0xA2);
    delay_ms(100);
    
    qmi8658a_read_reg(dev, QMI8658A_REG_COD_STATUS, &status, 1);
    
    return (status == 0x00) ? 0 : -1;
}

int qmi8658a_send_ctrl9_cmd(qmi8658a_t *dev, qmi8658a_ctrl9_cmd_t cmd)
{
    uint8_t status = 0;
    int count = 0;
    int retry = 3;
    
    while (retry-- > 0) {
        /* 写命令 */
        qmi8658a_write_reg(dev, QMI8658A_REG_CTRL9, (uint8_t)cmd);
        
        /* 等待命令完成 (bit7=1) */
        count = 0;
        qmi8658a_read_reg(dev, QMI8658A_REG_STATUS_INT, &status, 1);
        while (((status & 0x80) != 0x80) && (count++ < 100)) {
            delay_ms(1);
            qmi8658a_read_reg(dev, QMI8658A_REG_STATUS_INT, &status, 1);
        }
        if (count >= 100) continue;
        
        /* 写 ACK */
        qmi8658a_write_reg(dev, QMI8658A_REG_CTRL9, QMI8658A_CMD_ACK);
        
        /* 等待确认 (bit7=0) */
        count = 0;
        qmi8658a_read_reg(dev, QMI8658A_REG_STATUS_INT, &status, 1);
        while (((status & 0x80) == 0x80) && (count++ < 100)) {
            delay_ms(1);
            qmi8658a_read_reg(dev, QMI8658A_REG_STATUS_INT, &status, 1);
        }
        if (count < 100) {
            return 0;
        }
    }
    
    return -1;
}
