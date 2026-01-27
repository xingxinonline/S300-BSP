/**
 * @file    ft6x36.h
 * @brief   FT6X36 Capacitive Touch Controller Driver
 * @details 支持 FT6236/FT6336 系列电容触摸IC
 *          最多支持 2 点触摸
 */

#ifndef S300_BSP_FT6X36_H
#define S300_BSP_FT6X36_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/*===========================================================================
 * FT6X36 Register Definitions
 *===========================================================================*/

#define FT6X36_REG_DEV_MODE         0x00    /**< 设备模式 */
#define FT6X36_REG_GEST_ID          0x01    /**< 手势 ID */
#define FT6X36_REG_TD_STATUS        0x02    /**< 触摸点数 */

/* 触摸点 1 寄存器 */
#define FT6X36_REG_P1_XH            0x03    /**< 触摸点1 X高字节 + 事件标志 */
#define FT6X36_REG_P1_XL            0x04    /**< 触摸点1 X低字节 */
#define FT6X36_REG_P1_YH            0x05    /**< 触摸点1 Y高字节 + 触摸ID */
#define FT6X36_REG_P1_YL            0x06    /**< 触摸点1 Y低字节 */
#define FT6X36_REG_P1_WEIGHT        0x07    /**< 触摸点1 权重/压力 */
#define FT6X36_REG_P1_MISC          0x08    /**< 触摸点1 杂项 */

/* 触摸点 2 寄存器 */
#define FT6X36_REG_P2_XH            0x09    /**< 触摸点2 X高字节 + 事件标志 */
#define FT6X36_REG_P2_XL            0x0A    /**< 触摸点2 X低字节 */
#define FT6X36_REG_P2_YH            0x0B    /**< 触摸点2 Y高字节 + 触摸ID */
#define FT6X36_REG_P2_YL            0x0C    /**< 触摸点2 Y低字节 */
#define FT6X36_REG_P2_WEIGHT        0x0D    /**< 触摸点2 权重/压力 */
#define FT6X36_REG_P2_MISC          0x0E    /**< 触摸点2 杂项 */

/* 系统寄存器 */
#define FT6X36_REG_TH_GROUP         0x80    /**< 触摸阈值 */
#define FT6X36_REG_TH_DIFF          0x85    /**< 滤波阈值 */
#define FT6X36_REG_CTRL             0x86    /**< 控制寄存器 */
#define FT6X36_REG_TIMEENTERMONITOR 0x87    /**< 进入监控模式时间 */
#define FT6X36_REG_PERIODACTIVE     0x88    /**< 扫描周期 (活跃模式) */
#define FT6X36_REG_PERIODMONITOR    0x89    /**< 扫描周期 (监控模式) */
#define FT6X36_REG_RADIAN_VALUE     0x91    /**< 手势识别角度 */
#define FT6X36_REG_OFFSET_LR        0x92    /**< 左右偏移 */
#define FT6X36_REG_OFFSET_UD        0x93    /**< 上下偏移 */
#define FT6X36_REG_DISTANCE_LR      0x94    /**< 左右滑动距离 */
#define FT6X36_REG_DISTANCE_UD      0x95    /**< 上下滑动距离 */
#define FT6X36_REG_DISTANCE_ZOOM    0x96    /**< 缩放距离 */
#define FT6X36_REG_LIB_VER_H        0xA1    /**< 库版本高字节 */
#define FT6X36_REG_LIB_VER_L        0xA2    /**< 库版本低字节 */
#define FT6X36_REG_CIPHER           0xA3    /**< 芯片型号 */
#define FT6X36_REG_G_MODE           0xA4    /**< 中断模式 */
#define FT6X36_REG_PWR_MODE         0xA5    /**< 电源模式 */
#define FT6X36_REG_FIRMID           0xA6    /**< 固件版本 */
#define FT6X36_REG_FOCALTECH_ID     0xA8    /**< 厂商 ID */
#define FT6X36_REG_RELEASE_CODE     0xAF    /**< 发布代码 */
#define FT6X36_REG_STATE            0xBC    /**< 状态寄存器 */

/* 事件类型 (XH 高 2 位) */
#define FT6X36_EVENT_PRESS_DOWN     0x00    /**< 按下 */
#define FT6X36_EVENT_LIFT_UP        0x01    /**< 抬起 */
#define FT6X36_EVENT_CONTACT        0x02    /**< 接触中 */
#define FT6X36_EVENT_NO_EVENT       0x03    /**< 无事件 */

/* 手势 ID */
#define FT6X36_GEST_ID_NONE         0x00    /**< 无手势 */
#define FT6X36_GEST_ID_MOVE_UP      0x10    /**< 上滑 */
#define FT6X36_GEST_ID_MOVE_LEFT    0x14    /**< 左滑 */
#define FT6X36_GEST_ID_MOVE_DOWN    0x18    /**< 下滑 */
#define FT6X36_GEST_ID_MOVE_RIGHT   0x1C    /**< 右滑 */
#define FT6X36_GEST_ID_ZOOM_IN      0x48    /**< 放大 */
#define FT6X36_GEST_ID_ZOOM_OUT     0x49    /**< 缩小 */

/* 中断模式 */
#define FT6X36_G_MODE_INT_POLLING   0x00    /**< 轮询模式 */
#define FT6X36_G_MODE_INT_TRIGGER   0x01    /**< 触发模式 */

/* 电源模式 */
#define FT6X36_PWR_MODE_ACTIVE      0x00    /**< 活跃模式 */
#define FT6X36_PWR_MODE_MONITOR     0x01    /**< 监控模式 */
#define FT6X36_PWR_MODE_HIBERNATE   0x03    /**< 休眠模式 */

/*===========================================================================
 * Data Structures
 *===========================================================================*/

/** @brief 触摸点数据 */
typedef struct {
    uint16_t x;             /**< X 坐标 */
    uint16_t y;             /**< Y 坐标 */
    uint8_t  event;         /**< 事件类型: 0=按下, 1=抬起, 2=接触 */
    uint8_t  weight;        /**< 触摸压力 (0-255) */
    uint8_t  area;          /**< 触摸区域 */
} ft6x36_point_t;

/** @brief 触摸数据 */
typedef struct {
    uint8_t         touch_count;    /**< 当前触摸点数 (0-2) */
    uint8_t         gesture_id;     /**< 手势 ID */
    ft6x36_point_t  points[2];      /**< 触摸点数据 */
} ft6x36_touch_data_t;

/** @brief 设备信息 */
typedef struct {
    uint8_t  chip_id;       /**< 芯片型号 */
    uint8_t  firmware_id;   /**< 固件版本 */
    uint8_t  vendor_id;     /**< 厂商 ID */
    uint16_t lib_version;   /**< 库版本 */
} ft6x36_dev_info_t;

/** @brief FT6X36 设备句柄 */
typedef struct {
    void    *i2c;           /**< I2C 句柄 (i2c_soft_t*) */
    uint8_t  i2c_addr;      /**< 7-bit I2C 地址 */
    uint16_t max_x;         /**< 最大 X 坐标 (屏幕宽度) */
    uint16_t max_y;         /**< 最大 Y 坐标 (屏幕高度) */
    bool     swap_xy;       /**< 交换 X/Y */
    bool     invert_x;      /**< X 坐标反转 */
    bool     invert_y;      /**< Y 坐标反转 */
} ft6x36_t;

/*===========================================================================
 * API Functions
 *===========================================================================*/

/**
 * @brief  初始化 FT6X36 触摸控制器
 * @param  dev      设备句柄
 * @param  i2c      已初始化的 I2C 句柄 (i2c_soft_t*)
 * @param  i2c_addr 7-bit I2C 地址 (通常为 0x38)
 * @return 0 成功，-1 失败
 */
int ft6x36_init(ft6x36_t *dev, void *i2c, uint8_t i2c_addr);

/**
 * @brief  配置触摸屏参数
 * @param  dev       设备句柄
 * @param  max_x     最大 X 坐标
 * @param  max_y     最大 Y 坐标
 * @param  swap_xy   是否交换 X/Y
 * @param  invert_x  是否反转 X
 * @param  invert_y  是否反转 Y
 */
void ft6x36_config(ft6x36_t *dev, uint16_t max_x, uint16_t max_y, 
                   bool swap_xy, bool invert_x, bool invert_y);

/**
 * @brief  读取触摸数据
 * @param  dev   设备句柄
 * @param  data  输出触摸数据
 * @return 0 成功，-1 失败
 */
int ft6x36_read_touch(ft6x36_t *dev, ft6x36_touch_data_t *data);

/**
 * @brief  检查是否有触摸
 * @param  dev  设备句柄
 * @return 触摸点数 (0 表示无触摸)，-1 读取失败
 */
int ft6x36_get_touch_count(ft6x36_t *dev);

/**
 * @brief  读取设备信息
 * @param  dev   设备句柄
 * @param  info  输出设备信息
 * @return 0 成功，-1 失败
 */
int ft6x36_read_device_info(ft6x36_t *dev, ft6x36_dev_info_t *info);

/**
 * @brief  写入单个寄存器
 * @param  dev  设备句柄
 * @param  reg  寄存器地址
 * @param  val  要写入的值
 * @return 0 成功，-1 失败
 */
int ft6x36_write_reg(ft6x36_t *dev, uint8_t reg, uint8_t val);

/**
 * @brief  读取单个寄存器
 * @param  dev  设备句柄
 * @param  reg  寄存器地址
 * @param  val  输出读取的值
 * @return 0 成功，-1 失败
 */
int ft6x36_read_reg(ft6x36_t *dev, uint8_t reg, uint8_t *val);

/**
 * @brief  设置触摸阈值
 * @param  dev       设备句柄
 * @param  threshold 阈值 (默认 22)
 * @return 0 成功，-1 失败
 */
int ft6x36_set_threshold(ft6x36_t *dev, uint8_t threshold);

/**
 * @brief  设置中断模式
 * @param  dev   设备句柄
 * @param  mode  0=轮询, 1=触发
 * @return 0 成功，-1 失败
 */
int ft6x36_set_interrupt_mode(ft6x36_t *dev, uint8_t mode);

/**
 * @brief  进入休眠模式
 * @param  dev  设备句柄
 * @return 0 成功，-1 失败
 */
int ft6x36_enter_sleep(ft6x36_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_FT6X36_H */
