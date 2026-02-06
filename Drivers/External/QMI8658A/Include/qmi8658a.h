/**
 * @file    qmi8658a.h
 * @brief   QMI8658A 六轴IMU传感器驱动头文件
 * @details QMI8658A 是一款高性能六轴惯性测量单元(IMU)，
 *          集成三轴加速度计和三轴陀螺仪
 * 
 * @note    参考原始SDK实现，适配S300-BSP软件I2C驱动
 */

#ifndef S300_BSP_QMI8658A_H
#define S300_BSP_QMI8658A_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "i2c_soft.h"

/*===========================================================================
 * 宏定义
 *===========================================================================*/

/** @brief QMI8658A I2C 从设备地址 (SA0=1: 0x6A, SA0=0: 0x6B) */
#define QMI8658A_ADDR               0x6A

/** @brief 重力加速度常量 (m/s²) */
#define QMI8658A_ONE_G              9.807f

/** @brief 圆周率 */
#define QMI8658A_PI                 3.14159265358979323846f

/** @brief 校准采样次数 */
#define QMI8658A_MAX_CALI_COUNT     100

/** @brief 芯片 WHO_AM_I 期望值 */
#define QMI8658A_CHIP_ID            0x05

/** @brief 芯片 Revision 期望值 */
#define QMI8658A_REVISION_ID        0x7C

/*===========================================================================
 * 传感器使能标志
 *===========================================================================*/

#define QMI8658A_DISABLE_ALL        0x00    /**< 都不使能 */
#define QMI8658A_ACC_ENABLE         0x01    /**< 使能加速度计 */
#define QMI8658A_GYR_ENABLE         0x02    /**< 使能陀螺仪 */
#define QMI8658A_ACCGYR_ENABLE      0x03    /**< 都使能 */

/*===========================================================================
 * 寄存器定义
 *===========================================================================*/

/** @brief QMI8658A 寄存器地址枚举 */
typedef enum {
    QMI8658A_REG_WHO_AM_I      = 0,
    QMI8658A_REG_REVISION      = 1,
    QMI8658A_REG_CTRL1         = 2,
    QMI8658A_REG_CTRL2         = 3,
    QMI8658A_REG_CTRL3         = 4,
    QMI8658A_REG_CTRL5         = 6,
    QMI8658A_REG_CTRL7         = 8,
    QMI8658A_REG_CTRL8         = 9,
    QMI8658A_REG_CTRL9         = 10,
    QMI8658A_REG_CAL1_L        = 11,
    QMI8658A_REG_CAL1_H        = 12,
    QMI8658A_REG_FIFO_WMK_TH   = 19,
    QMI8658A_REG_FIFO_CTRL     = 20,
    QMI8658A_REG_FIFO_COUNT    = 21,
    QMI8658A_REG_FIFO_STATUS   = 22,
    QMI8658A_REG_FIFO_DATA     = 23,
    QMI8658A_REG_STATUS_INT    = 45,
    QMI8658A_REG_STATUS0       = 46,
    QMI8658A_REG_STATUS1       = 47,
    QMI8658A_REG_TIMESTAMP_L   = 48,
    QMI8658A_REG_TIMESTAMP_M   = 49,
    QMI8658A_REG_TIMESTAMP_H   = 50,
    QMI8658A_REG_TEMP_L        = 51,
    QMI8658A_REG_TEMP_H        = 52,
    QMI8658A_REG_AX_L          = 53,
    QMI8658A_REG_AX_H          = 54,
    QMI8658A_REG_AY_L          = 55,
    QMI8658A_REG_AY_H          = 56,
    QMI8658A_REG_AZ_L          = 57,
    QMI8658A_REG_AZ_H          = 58,
    QMI8658A_REG_GX_L          = 59,
    QMI8658A_REG_GX_H          = 60,
    QMI8658A_REG_GY_L          = 61,
    QMI8658A_REG_GY_H          = 62,
    QMI8658A_REG_GZ_L          = 63,
    QMI8658A_REG_GZ_H          = 64,
    QMI8658A_REG_COD_STATUS    = 70,
    QMI8658A_REG_RESET         = 96
} qmi8658a_reg_t;

/*===========================================================================
 * CTRL9 命令定义
 *===========================================================================*/

typedef enum {
    QMI8658A_CMD_ACK                 = 0x00,
    QMI8658A_CMD_RST_FIFO            = 0x04,
    QMI8658A_CMD_REQ_FIFO            = 0x05,
    QMI8658A_CMD_WOM_SETTING         = 0x08,
    QMI8658A_CMD_ACCEL_HOST_OFFSET   = 0x09,
    QMI8658A_CMD_GYRO_HOST_OFFSET    = 0x0A,
    QMI8658A_CMD_CFG_TAP             = 0x0C,
    QMI8658A_CMD_CFG_PEDOMETER       = 0x0D,
    QMI8658A_CMD_MOTION              = 0x0E,
    QMI8658A_CMD_RST_PEDOMETER       = 0x0F,
    QMI8658A_CMD_COPY_USID           = 0x10,
    QMI8658A_CMD_SET_RPU             = 0x11,
    QMI8658A_CMD_AHB_CLOCK_GATING    = 0x12,
    QMI8658A_CMD_ON_DEMAND_CALI      = 0xA2,
    QMI8658A_CMD_APPLY_GYRO_GAINS    = 0xAA
} qmi8658a_ctrl9_cmd_t;

/*===========================================================================
 * 加速度计配置
 *===========================================================================*/

/** @brief 加速度计量程 */
typedef enum {
    QMI8658A_ACC_RANGE_2G   = 0x00 << 4,
    QMI8658A_ACC_RANGE_4G   = 0x01 << 4,
    QMI8658A_ACC_RANGE_8G   = 0x02 << 4,
    QMI8658A_ACC_RANGE_16G  = 0x03 << 4
} qmi8658a_acc_range_t;

/** @brief 加速度计输出数据率 */
typedef enum {
    QMI8658A_ACC_ODR_8000HZ     = 0x00,
    QMI8658A_ACC_ODR_4000HZ     = 0x01,
    QMI8658A_ACC_ODR_2000HZ     = 0x02,
    QMI8658A_ACC_ODR_1000HZ     = 0x03,
    QMI8658A_ACC_ODR_500HZ      = 0x04,
    QMI8658A_ACC_ODR_250HZ      = 0x05,
    QMI8658A_ACC_ODR_125HZ      = 0x06,
    QMI8658A_ACC_ODR_62_5HZ     = 0x07,
    QMI8658A_ACC_ODR_31_25HZ    = 0x08,
    QMI8658A_ACC_ODR_LP_128HZ   = 0x0C,
    QMI8658A_ACC_ODR_LP_21HZ    = 0x0D,
    QMI8658A_ACC_ODR_LP_11HZ    = 0x0E,
    QMI8658A_ACC_ODR_LP_3HZ     = 0x0F
} qmi8658a_acc_odr_t;

/*===========================================================================
 * 陀螺仪配置
 *===========================================================================*/

/** @brief 陀螺仪量程 */
typedef enum {
    QMI8658A_GYR_RANGE_16DPS    = 0 << 4,
    QMI8658A_GYR_RANGE_32DPS    = 1 << 4,
    QMI8658A_GYR_RANGE_64DPS    = 2 << 4,
    QMI8658A_GYR_RANGE_128DPS   = 3 << 4,
    QMI8658A_GYR_RANGE_256DPS   = 4 << 4,
    QMI8658A_GYR_RANGE_512DPS   = 5 << 4,
    QMI8658A_GYR_RANGE_1024DPS  = 6 << 4,
    QMI8658A_GYR_RANGE_2048DPS  = 7 << 4
} qmi8658a_gyr_range_t;

/** @brief 陀螺仪输出数据率 */
typedef enum {
    QMI8658A_GYR_ODR_8000HZ     = 0x00,
    QMI8658A_GYR_ODR_4000HZ     = 0x01,
    QMI8658A_GYR_ODR_2000HZ     = 0x02,
    QMI8658A_GYR_ODR_1000HZ     = 0x03,
    QMI8658A_GYR_ODR_500HZ      = 0x04,
    QMI8658A_GYR_ODR_250HZ      = 0x05,
    QMI8658A_GYR_ODR_125HZ      = 0x06,
    QMI8658A_GYR_ODR_62_5HZ     = 0x07,
    QMI8658A_GYR_ODR_31_25HZ    = 0x08
} qmi8658a_gyr_odr_t;

/*===========================================================================
 * 低通滤波器/自检配置
 *===========================================================================*/

typedef enum {
    QMI8658A_LPF_DISABLE = 0,
    QMI8658A_LPF_ENABLE  = 1
} qmi8658a_lpf_cfg_t;

typedef enum {
    QMI8658A_ST_DISABLE = 0,
    QMI8658A_ST_ENABLE  = 1
} qmi8658a_st_cfg_t;

/** @brief 低通滤波器模式 */
typedef enum {
    QMI8658A_ACC_LPF_MODE_0 = 0x00 << 1,
    QMI8658A_ACC_LPF_MODE_1 = 0x01 << 1,
    QMI8658A_ACC_LPF_MODE_2 = 0x02 << 1,
    QMI8658A_ACC_LPF_MODE_3 = 0x03 << 1,
    QMI8658A_GYR_LPF_MODE_0 = 0x00 << 5,
    QMI8658A_GYR_LPF_MODE_1 = 0x01 << 5,
    QMI8658A_GYR_LPF_MODE_2 = 0x02 << 5,
    QMI8658A_GYR_LPF_MODE_3 = 0x03 << 5
} qmi8658a_lpf_mode_t;

/*===========================================================================
 * 传感器配置结构体
 *===========================================================================*/

typedef struct {
    uint8_t             enable_flags;   /**< 传感器使能标志 */
    qmi8658a_acc_range_t acc_range;     /**< 加速度计量程 */
    qmi8658a_acc_odr_t   acc_odr;       /**< 加速度计ODR */
    qmi8658a_gyr_range_t gyr_range;     /**< 陀螺仪量程 */
    qmi8658a_gyr_odr_t   gyr_odr;       /**< 陀螺仪ODR */
} qmi8658a_config_t;

/*===========================================================================
 * 传感器句柄结构体
 *===========================================================================*/

typedef struct {
    i2c_soft_t         *i2c;            /**< 软件I2C句柄 */
    uint8_t             addr;           /**< I2C从设备地址 */
    qmi8658a_config_t   cfg;            /**< 传感器配置 */
    uint16_t            ssvt_a;         /**< 加速度计灵敏度 */
    uint16_t            ssvt_g;         /**< 陀螺仪灵敏度 */
    float               offset_acc[3];  /**< 加速度计偏移 */
    float               offset_gyr[3];  /**< 陀螺仪偏移 */
    float               last_acc[3];    /**< 上次加速度值 */
    float               last_gyr[3];    /**< 上次陀螺仪值 */
    bool                calibrated;     /**< 是否已校准 */
} qmi8658a_t;

/*===========================================================================
 * 传感器数据结构体
 *===========================================================================*/

typedef struct {
    float acc[3];   /**< 加速度 X,Y,Z (m/s²) */
    float gyr[3];   /**< 角速度 X,Y,Z (rad/s) */
    float temp;     /**< 温度 (°C) */
} qmi8658a_data_t;

/*===========================================================================
 * API 函数声明
 *===========================================================================*/

/**
 * @brief   初始化 QMI8658A 传感器
 * @param   dev     传感器句柄
 * @param   i2c     软件I2C句柄
 * @param   addr    I2C从设备地址 (通常为 QMI8658A_ADDR)
 * @return  0: 成功, -1: 失败
 */
int qmi8658a_init(qmi8658a_t *dev, i2c_soft_t *i2c, uint8_t addr);

/**
 * @brief   复位传感器
 * @param   dev     传感器句柄
 */
void qmi8658a_reset(qmi8658a_t *dev);

/**
 * @brief   检查 WHO_AM_I 寄存器
 * @param   dev     传感器句柄
 * @return  0: 正确, -1: 错误
 */
int qmi8658a_check_id(qmi8658a_t *dev);

/**
 * @brief   配置加速度计参数
 * @param   dev         传感器句柄
 * @param   range       量程
 * @param   odr         输出数据率
 * @param   lpf_enable  是否使能低通滤波
 * @param   st_enable   是否使能自检
 */
void qmi8658a_config_acc(qmi8658a_t *dev, qmi8658a_acc_range_t range,
                         qmi8658a_acc_odr_t odr, qmi8658a_lpf_cfg_t lpf_enable,
                         qmi8658a_st_cfg_t st_enable);

/**
 * @brief   配置陀螺仪参数
 * @param   dev         传感器句柄
 * @param   range       量程
 * @param   odr         输出数据率
 * @param   lpf_enable  是否使能低通滤波
 * @param   st_enable   是否使能自检
 */
void qmi8658a_config_gyr(qmi8658a_t *dev, qmi8658a_gyr_range_t range,
                         qmi8658a_gyr_odr_t odr, qmi8658a_lpf_cfg_t lpf_enable,
                         qmi8658a_st_cfg_t st_enable);

/**
 * @brief   使能/禁用传感器
 * @param   dev         传感器句柄
 * @param   enable_flags 使能标志 (QMI8658A_ACC_ENABLE, QMI8658A_GYR_ENABLE 等)
 */
void qmi8658a_enable_sensors(qmi8658a_t *dev, uint8_t enable_flags);

/**
 * @brief   读取传感器数据 (加速度和陀螺仪)
 * @param   dev     传感器句柄
 * @param   data    输出数据结构体
 * @return  0: 成功, -1: 数据未就绪
 */
int qmi8658a_read_data(qmi8658a_t *dev, qmi8658a_data_t *data);

/**
 * @brief   获取传感器温度
 * @param   dev     传感器句柄
 * @return  温度值 (°C)
 */
float qmi8658a_read_temperature(qmi8658a_t *dev);

/**
 * @brief   陀螺仪按需校准
 * @param   dev     传感器句柄
 * @return  0: 成功, -1: 失败
 */
int qmi8658a_calibration(qmi8658a_t *dev);

/**
 * @brief   发送 CTRL9 命令
 * @param   dev     传感器句柄
 * @param   cmd     命令
 * @return  0: 成功, -1: 失败
 */
int qmi8658a_send_ctrl9_cmd(qmi8658a_t *dev, qmi8658a_ctrl9_cmd_t cmd);

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_QMI8658A_H */
