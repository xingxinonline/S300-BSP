/**
 * @file    bus_servo.h
 * @brief   总线舵机驱动 (Hiwonder/幻尔科技 协议)
 * @details 支持半双工 UART 通信的总线舵机控制
 *          - 波特率: 115200 bps
 *          - 角度范围: 0~240° (协议值 0~1000)
 *          - 时间范围: 0~30000 ms
 *          - ID 范围: 0~253 (254 为广播 ID)
 * 
 * @note    硬件连接:
 *          - MOTO_BUSEN (GPIO) 控制半双工方向切换
 *          - 通过 SN74LVC1G3157 模拟开关选择 TX/RX
 *          - Si2302CDS NMOS 进行 1.8V 到 5V 电平转换
 */

#ifndef S300_BSP_BUS_SERVO_H
#define S300_BSP_BUS_SERVO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/*===========================================================================
 * Protocol Definitions (协议定义)
 *===========================================================================*/

/** @brief 帧头标识 */
#define BUS_SERVO_FRAME_HEADER          0x55

/** @brief 广播 ID (所有舵机接收，不返回应答) */
#define BUS_SERVO_BROADCAST_ID          0xFE

/** @brief 默认舵机 ID */
#define BUS_SERVO_DEFAULT_ID            0x01

/** @brief 最大舵机 ID */
#define BUS_SERVO_MAX_ID                253

/** @brief 协议波特率 */
#define BUS_SERVO_BAUDRATE              115200

/** @brief 最大指令包长度 */
#define BUS_SERVO_MAX_PACKET_LEN        16

/** @brief 角度范围 (0~1000 对应 0°~240°) */
#define BUS_SERVO_ANGLE_MIN             0
#define BUS_SERVO_ANGLE_MAX             1000
#define BUS_SERVO_ANGLE_DEGREES         240.0f

/** @brief 时间范围 (0~30000 ms) */
#define BUS_SERVO_TIME_MIN              0
#define BUS_SERVO_TIME_MAX              30000

/** @brief 偏差范围 (-125~125 对应 -30°~30°) */
#define BUS_SERVO_OFFSET_MIN            (-125)
#define BUS_SERVO_OFFSET_MAX            125

/*===========================================================================
 * Command Definitions (指令定义)
 *===========================================================================*/

/** @brief 写指令 */
typedef enum {
    BUS_SERVO_CMD_MOVE_TIME_WRITE       = 1,    /**< 设置角度和时间，立即执行 */
    BUS_SERVO_CMD_MOVE_TIME_READ        = 2,    /**< 读取设置的角度和时间 */
    BUS_SERVO_CMD_MOVE_TIME_WAIT_WRITE  = 7,    /**< 预设角度和时间，等待启动 */
    BUS_SERVO_CMD_MOVE_TIME_WAIT_READ   = 8,    /**< 读取预设的角度和时间 */
    BUS_SERVO_CMD_MOVE_START            = 11,   /**< 启动预设运动 */
    BUS_SERVO_CMD_MOVE_STOP             = 12,   /**< 停止运动 */
    BUS_SERVO_CMD_ID_WRITE              = 13,   /**< 设置舵机 ID */
    BUS_SERVO_CMD_ID_READ               = 14,   /**< 读取舵机 ID */
    BUS_SERVO_CMD_ANGLE_OFFSET_ADJUST   = 17,   /**< 调整角度偏差 (不保存) */
    BUS_SERVO_CMD_ANGLE_OFFSET_WRITE    = 18,   /**< 保存角度偏差 */
    BUS_SERVO_CMD_ANGLE_OFFSET_READ     = 19,   /**< 读取角度偏差 */
    BUS_SERVO_CMD_ANGLE_LIMIT_WRITE     = 20,   /**< 设置角度限制 */
    BUS_SERVO_CMD_ANGLE_LIMIT_READ      = 21,   /**< 读取角度限制 */
    BUS_SERVO_CMD_VIN_LIMIT_WRITE       = 22,   /**< 设置输入电压限制 */
    BUS_SERVO_CMD_VIN_LIMIT_READ        = 23,   /**< 读取输入电压限制 */
    BUS_SERVO_CMD_TEMP_LIMIT_WRITE      = 24,   /**< 设置最高温度限制 */
    BUS_SERVO_CMD_TEMP_LIMIT_READ       = 25,   /**< 读取最高温度限制 */
    BUS_SERVO_CMD_TEMP_READ             = 26,   /**< 读取当前温度 */
    BUS_SERVO_CMD_VIN_READ              = 27,   /**< 读取当前输入电压 */
    BUS_SERVO_CMD_POS_READ              = 28,   /**< 读取当前角度位置 */
    BUS_SERVO_CMD_MODE_WRITE            = 29,   /**< 设置舵机/电机模式 */
    BUS_SERVO_CMD_MODE_READ             = 30,   /**< 读取模式 */
    BUS_SERVO_CMD_LOAD_WRITE            = 31,   /**< 设置电机装载/卸载 */
    BUS_SERVO_CMD_LOAD_READ             = 32,   /**< 读取电机状态 */
    BUS_SERVO_CMD_LED_CTRL_WRITE        = 33,   /**< 设置 LED 状态 */
    BUS_SERVO_CMD_LED_CTRL_READ         = 34,   /**< 读取 LED 状态 */
    BUS_SERVO_CMD_LED_ERROR_WRITE       = 35,   /**< 设置 LED 报警 */
    BUS_SERVO_CMD_LED_ERROR_READ        = 36,   /**< 读取 LED 报警 */
    BUS_SERVO_CMD_DISTANCE_READ         = 48,   /**< 读取转动距离 */
} bus_servo_cmd_t;

/*===========================================================================
 * Data Structures (数据结构)
 *===========================================================================*/

/** @brief 舵机工作模式 */
typedef enum {
    BUS_SERVO_MODE_POSITION = 0,    /**< 位置控制模式 (舵机模式) */
    BUS_SERVO_MODE_MOTOR    = 1,    /**< 电机控制模式 (连续旋转) */
} bus_servo_mode_t;

/** @brief 电机转动模式 (仅电机模式有效) */
typedef enum {
    BUS_SERVO_MOTOR_DUTY  = 0,      /**< 固定占空比模式 (速度 -1000~1000) */
    BUS_SERVO_MOTOR_SPEED = 1,      /**< 固定转速模式 (速度 -50~50) */
} bus_servo_motor_mode_t;

/** @brief LED 报警类型 (可组合) */
typedef enum {
    BUS_SERVO_LED_ERROR_NONE        = 0,    /**< 无报警 */
    BUS_SERVO_LED_ERROR_OVER_TEMP   = 1,    /**< 过温报警 */
    BUS_SERVO_LED_ERROR_OVER_VIN    = 2,    /**< 过压报警 */
    BUS_SERVO_LED_ERROR_STALL       = 4,    /**< 堵转报警 */
} bus_servo_led_error_t;

/** @brief 舵机状态信息 */
typedef struct {
    uint8_t  id;                    /**< 舵机 ID */
    int16_t  position;              /**< 当前位置 (0~1000 或负值) */
    float    angle;                 /**< 当前角度 (度) */
    uint16_t vin_mv;                /**< 输入电压 (mV) */
    uint8_t  temperature;           /**< 温度 (°C) */
    uint8_t  mode;                  /**< 工作模式 */
    bool     loaded;                /**< 电机是否装载 */
} bus_servo_status_t;

/** @brief 舵机控制器句柄 */
typedef struct {
    uint8_t  uart_idx;              /**< UART 索引 (0~3) */
    uint8_t  dir_port;              /**< 方向控制 GPIO 端口 */
    uint8_t  dir_pin;               /**< 方向控制 GPIO 引脚 */
    uint32_t rx_timeout_us;         /**< 接收超时 (微秒) */
} bus_servo_t;

/*===========================================================================
 * API Functions (接口函数)
 *===========================================================================*/

/**
 * @brief  初始化总线舵机控制器
 * @param  servo       舵机控制器句柄
 * @param  uart_idx    UART 索引 (0~3)
 * @param  dir_pin     方向控制 GPIO 引脚
 * @param  sysclk_hz   系统时钟频率 (Hz)
 * @return 0 成功，其他失败
 */
int bus_servo_init(bus_servo_t *servo, uint8_t uart_idx, uint8_t dir_pin, uint32_t sysclk_hz);

/**
 * @brief  反初始化总线舵机控制器
 * @param  servo       舵机控制器句柄
 */
void bus_servo_deinit(bus_servo_t *servo);

/*---------------------------------------------------------------------------
 * 运动控制
 *---------------------------------------------------------------------------*/

/**
 * @brief  设置舵机角度 (立即执行)
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID (0~253, 254=广播)
 * @param  angle       目标角度 (0~240°)
 * @param  time_ms     运动时间 (0~30000 ms)
 * @return 0 成功，其他失败
 */
int bus_servo_move(bus_servo_t *servo, uint8_t id, float angle, uint16_t time_ms);

/**
 * @brief  设置舵机位置值 (立即执行)
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @param  position    目标位置 (0~1000)
 * @param  time_ms     运动时间 (0~30000 ms)
 * @return 0 成功，其他失败
 */
int bus_servo_move_raw(bus_servo_t *servo, uint8_t id, uint16_t position, uint16_t time_ms);

/**
 * @brief  预设舵机角度 (等待启动信号)
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @param  angle       目标角度 (0~240°)
 * @param  time_ms     运动时间 (0~30000 ms)
 * @return 0 成功，其他失败
 */
int bus_servo_move_prepare(bus_servo_t *servo, uint8_t id, float angle, uint16_t time_ms);

/**
 * @brief  启动预设运动
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @return 0 成功，其他失败
 */
int bus_servo_move_start(bus_servo_t *servo, uint8_t id);

/**
 * @brief  停止舵机运动
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @return 0 成功，其他失败
 */
int bus_servo_stop(bus_servo_t *servo, uint8_t id);

/*---------------------------------------------------------------------------
 * 状态查询
 *---------------------------------------------------------------------------*/

/**
 * @brief  读取舵机当前角度
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @param  angle       返回角度值 (度)
 * @return 0 成功，其他失败
 */
int bus_servo_read_angle(bus_servo_t *servo, uint8_t id, float *angle);

/**
 * @brief  读取舵机当前位置
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @param  position    返回位置值 (可能为负)
 * @return 0 成功，其他失败
 */
int bus_servo_read_position(bus_servo_t *servo, uint8_t id, int16_t *position);

/**
 * @brief  读取舵机输入电压
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @param  vin_mv      返回电压值 (mV)
 * @return 0 成功，其他失败
 */
int bus_servo_read_vin(bus_servo_t *servo, uint8_t id, uint16_t *vin_mv);

/**
 * @brief  读取舵机温度
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @param  temp        返回温度值 (°C)
 * @return 0 成功，其他失败
 */
int bus_servo_read_temp(bus_servo_t *servo, uint8_t id, uint8_t *temp);

/**
 * @brief  读取舵机加载状态
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @param  load        返回加载状态 (1=加载, 0=卸载)
 * @return 0 成功，其他失败
 */
int bus_servo_read_load(bus_servo_t *servo, uint8_t id, uint8_t *load);

/**
 * @brief  读取舵机 ID (广播查询)
 * @param  servo       舵机控制器句柄
 * @param  id          返回舵机 ID
 * @return 0 成功，其他失败
 * @note   总线上只能连接一个舵机时使用
 */
int bus_servo_read_id(bus_servo_t *servo, uint8_t *id);

/**
 * @brief  读取舵机完整状态
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @param  status      返回状态结构
 * @return 0 成功，其他失败
 */
int bus_servo_read_status(bus_servo_t *servo, uint8_t id, bus_servo_status_t *status);

/*---------------------------------------------------------------------------
 * 配置设置
 *---------------------------------------------------------------------------*/

/**
 * @brief  设置舵机 ID
 * @param  servo       舵机控制器句柄
 * @param  old_id      当前舵机 ID
 * @param  new_id      新舵机 ID (0~253)
 * @return 0 成功，其他失败
 */
int bus_servo_set_id(bus_servo_t *servo, uint8_t old_id, uint8_t new_id);

/**
 * @brief  装载/卸载舵机电机
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @param  load        true=装载(有力矩), false=卸载(无力矩)
 * @return 0 成功，其他失败
 */
int bus_servo_set_load(bus_servo_t *servo, uint8_t id, bool load);

/**
 * @brief  设置舵机工作模式
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @param  mode        工作模式 (位置/电机)
 * @param  motor_mode  电机模式下的转动模式
 * @param  speed       电机模式下的转速 (-1000~1000 或 -50~50)
 * @return 0 成功，其他失败
 */
int bus_servo_set_mode(bus_servo_t *servo, uint8_t id, bus_servo_mode_t mode,
                       bus_servo_motor_mode_t motor_mode, int16_t speed);

/**
 * @brief  调整角度偏差 (不保存)
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @param  offset      偏差值 (-125~125, 对应 -30°~30°)
 * @return 0 成功，其他失败
 */
int bus_servo_adjust_offset(bus_servo_t *servo, uint8_t id, int8_t offset);

/**
 * @brief  保存角度偏差
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @return 0 成功，其他失败
 */
int bus_servo_save_offset(bus_servo_t *servo, uint8_t id);

/**
 * @brief  设置角度限制
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @param  min_angle   最小角度 (0~240°)
 * @param  max_angle   最大角度 (0~240°)
 * @return 0 成功，其他失败
 */
int bus_servo_set_angle_limit(bus_servo_t *servo, uint8_t id, float min_angle, float max_angle);

/**
 * @brief  设置 LED 控制
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @param  off         true=常灭, false=常亮
 * @return 0 成功，其他失败
 */
int bus_servo_set_led(bus_servo_t *servo, uint8_t id, bool off);

/**
 * @brief  设置 LED 报警
 * @param  servo       舵机控制器句柄
 * @param  id          舵机 ID
 * @param  error_mask  报警类型掩码 (bus_servo_led_error_t 组合)
 * @return 0 成功，其他失败
 */
int bus_servo_set_led_error(bus_servo_t *servo, uint8_t id, uint8_t error_mask);

/*---------------------------------------------------------------------------
 * 工具函数
 *---------------------------------------------------------------------------*/

/**
 * @brief  角度转换为位置值
 * @param  angle       角度 (0~240°)
 * @return 位置值 (0~1000)
 */
static inline uint16_t bus_servo_angle_to_pos(float angle)
{
    if (angle < 0.0f) angle = 0.0f;
    if (angle > BUS_SERVO_ANGLE_DEGREES) angle = BUS_SERVO_ANGLE_DEGREES;
    return (uint16_t)((angle / BUS_SERVO_ANGLE_DEGREES) * BUS_SERVO_ANGLE_MAX);
}

/**
 * @brief  位置值转换为角度
 * @param  position    位置值 (可能为负)
 * @return 角度 (度)
 */
static inline float bus_servo_pos_to_angle(int16_t position)
{
    return ((float)position / BUS_SERVO_ANGLE_MAX) * BUS_SERVO_ANGLE_DEGREES;
}

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_BUS_SERVO_H */
