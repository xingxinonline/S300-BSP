/**
 * @file    main.c
 * @brief   QMI8658A 陀螺仪/IMU 自稳云台演示程序
 * @details 演示 QMI8658A 六轴IMU传感器配合总线舵机实现自稳:
 *          - 初始化 I2C 和 QMI8658A 传感器
 *          - 读取加速度计数据
 *          - 直接使用加速度计计算相对于重力的倾斜角度
 *          - 使用舵机2和舵机3补偿，保持与地面垂直
 * 
 * @note    硬件连接 (gimbal_master):
 *          - I2C3: GPIOA4(SCL), GPIOA5(SDA) - IMU
 *          - UART2: GPIOA23(TX), GPIOA24(RX) - 调试串口
 *          - UART3: GPIOA27(TX), GPIOA26(RX) - 总线舵机
 *          - GPIO22: MOTO_BUSEN - 舵机方向控制
 */

#include "s300.h"
#include "board.h"
#include "uart.h"
#include "gpio.h"
#include "rcc.h"
#include "i2c_soft.h"
#include "qmi8658a.h"
#include "bus_servo.h"
#include <stdio.h>
#include <stdlib.h>  /* for abs() */
#include <math.h>

/*===========================================================================
 * 宏定义
 *===========================================================================*/

/** 
 * @brief 测试模式开关
 * 0 = 正常稳定模式
 * 1 = 轴向测试模式（测试舵机与IMU的对应关系）
 * 2 = IMU只读模式（舵机失能，只读取IMU数据）
 */
#define TEST_MODE           0

/** @brief I2C 使用的索引 (I2C3: GPIOA4-SCL, GPIOA5-SDA) */
#define IMU_I2C_IDX         3

/** @brief I2C 总线频率 */
#define IMU_I2C_FREQ        400000

/** @brief 姿态解算采样周期 (秒) */
#define IMU_SAMPLE_DT       0.01f

/** @brief 采样延时 (毫秒) */
#define IMU_SAMPLE_MS       10

/** @brief 打印间隔 (采样周期数) */
#define PRINT_INTERVAL      10

/*---------------------------------------------------------------------------
 * 舵机配置 (参考 Tracking_Demo 的 gimbal_ctrl)
 *---------------------------------------------------------------------------*/

/** @brief Roll 补偿舵机 ID */
#define SERVO_ROLL_ID       3

/** @brief Pitch 补偿舵机 ID */
#define SERVO_PITCH_ID      2

/** 
 * @brief 舵机脉冲范围
 * 参考 LeArm: 2号、3号舵机范围是 -90° ~ +90°
 * 中心 500，每度 4.167 脉冲，±90° 对应 ±375 脉冲
 * 即脉冲范围 125 ~ 875
 */
#define ROLL_MIN_PULSE      125
#define ROLL_MID_PULSE      500
#define ROLL_MAX_PULSE      875

#define PITCH_MIN_PULSE     125
#define PITCH_MID_PULSE     500
#define PITCH_MAX_PULSE     875

/** 
 * @brief 角度范围 (度) - 中心为 0
 * 2号、3号舵机实际行程为 -90° ~ +90°
 */
#define ANGLE_MIN           (-90.0f)
#define ANGLE_MAX           (90.0f)

/** @brief 舵机移动时间 (ms) - 越小响应越快 */
#define SERVO_MOVE_TIME     20

/** @brief 死区阈值 (度) - 小于此值不补偿，需略大于 IMU 噪声 */
#define DEADZONE_DEG        0.8f

/*---------------------------------------------------------------------------
 * 目标角度 (基于 IMU 安装方向分析得出)
 * 
 * IMU 安装方式：Y 轴朝下时为"垂直"
 * - 理想状态：Acc = (0, -g, 0)
 * - Roll  = atan2(Y, Z) = atan2(-g, 0) = -90°
 * - Pitch = atan2(-X, sqrt(Y²+Z²)) = atan2(0, g) = 0°
 *---------------------------------------------------------------------------*/

/** @brief Roll 目标角度 (度) - Y轴朝下时 Roll = -90° */
#define TARGET_ROLL         (-90.0f)

/** @brief Pitch 目标角度 (度) - X轴水平时 Pitch = 0° */
#define TARGET_PITCH        (0.0f)

/*---------------------------------------------------------------------------
 * PID 控制参数 (参考 Tracking_Demo/Aiming_Demo 优化)
 *---------------------------------------------------------------------------*/

/** @brief Roll PID 参数 */
#define ROLL_KP             1.2f    /* 比例增益 - 略降以减少过冲 */
#define ROLL_KI             0.5f    /* 积分增益 - 增大以更快消除稳态误差 */
#define ROLL_KD             0.15f   /* 微分增益 - 增大以抑制过冲 */

/** @brief Pitch PID 参数 */
#define PITCH_KP            1.2f
#define PITCH_KI            0.5f
#define PITCH_KD            0.15f

/** @brief 积分限幅 */
#define INTEGRAL_MAX        200.0f  /* 积分限幅 - 增大防止饱和导致稳态误差 */

/** @brief 输入低通滤波系数 - 平滑 IMU 读数 */
#define LPF_ALPHA           0.08f   /* 第一级滤波 */

/** @brief 微分项滤波系数 */
#define DERIV_LPF_ALPHA     0.15f

/** 
 * @brief 输出滤波系数 (借鉴 Aiming_Demo)
 * 第二级滤波，平滑舵机输出角度
 */
#define OUTPUT_LPF_ALPHA    0.3f

/** 
 * @brief 最小移动阈值 (度) - 借鉴 Tracking_Demo
 * 输出变化小于此值不发送舵机指令，减少抖动
 */
#define MIN_MOVE_THRESHOLD  0.5f

/** @brief 舵机输出变化限幅 (度/周期) - 防止突变 */
#define SERVO_RATE_LIMIT    4.0f    /* 渐进过渡 */

/** 
 * @brief 角度到脉冲转换系数
 * 舵机行程：0-1000 脉冲 对应 0-240°
 * 所以 1° = 1000/240 ≈ 4.17 脉冲
 */
#define PULSE_PER_DEGREE    4.17f

/*===========================================================================
 * 私有函数
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
 * @brief 限幅函数
 */
static float clamp(float value, float min_val, float max_val)
{
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

/**
 * @brief 使用加速度计计算倾斜角度
 * @param acc  加速度数据 [X, Y, Z] (单位: m/s²)
 * @param tilt_y 输出: Roll 倾斜角度 (绕 X 轴旋转)
 * @param tilt_z 输出: Pitch 倾斜角度 (绕 Y 轴旋转)
 * 
 * 假设设备正常放置时 Z 轴朝上 (Acc Z ≈ +9.8 m/s²)
 * Roll 角度 = atan2(Y, Z)  -- 侧倾
 * Pitch 角度 = atan2(-X, sqrt(Y²+Z²)) -- 俯仰
 */
static void calc_tilt_from_accel(float *acc, float *tilt_y, float *tilt_z)
{
    float ax = acc[0];
    float ay = acc[1];
    float az = acc[2];
    
    /* Roll: 绕 X 轴旋转，用 Y 和 Z 计算 */
    *tilt_y = atan2f(ay, az) * 57.29578f;  /* 180/PI */
    
    /* Pitch: 绕 Y 轴旋转，用 X 和 sqrt(Y²+Z²) 计算 */
    float yz_mag = sqrtf(ay * ay + az * az);
    *tilt_z = atan2f(-ax, yz_mag) * 57.29578f;
}

/*===========================================================================
 * 主函数
 *===========================================================================*/

int main(void)
{
    i2c_soft_t i2c;
    qmi8658a_t imu_sensor;
    qmi8658a_data_t sensor_data;
    bus_servo_t servo;
    uint32_t sample_count = 0;
    float temperature;
    float servo_roll_angle = 0.0f;
    float servo_pitch_angle = 0.0f;
    float tilt_y = 0.0f;  /* Y轴倾斜角度 */
    float tilt_z = 0.0f;  /* Z轴倾斜角度 */
    int roll_pulse = ROLL_MID_PULSE;
    int pitch_pulse = PITCH_MID_PULSE;
    bool stabilize_enabled = true;
    int ret;
    
    /* 零点校准变量 */
    bool zero_calibrated = false;  /* 零点校准完成标志 */
    int zero_cal_count = 0;        /* 零点校准采样计数 */
    
    /* PID 控制变量 */
    float roll_integral = 0.0f;
    float roll_last_error = 0.0f;
    float pitch_integral = 0.0f;
    float pitch_last_error = 0.0f;
    
    /* 微分项滤波变量 */
    float roll_deriv_filtered = 0.0f;
    float pitch_deriv_filtered = 0.0f;
    
    /* 低通滤波变量 */
    float filtered_roll = 0.0f;
    float filtered_pitch = 0.0f;
    bool filter_initialized = false;
    
    /* 舵机输出平滑变量 */
    float last_roll_output = 0.0f;
    float last_pitch_output = 0.0f;
    
    /* 输出滤波变量 (借鉴 Aiming_Demo 的双层滤波) */
    float filtered_out_roll = 0.0f;
    float filtered_out_pitch = 0.0f;
    
    /* 上次发送的脉冲值 (用于最小移动阈值判断) */
    int last_sent_roll_pulse = ROLL_MID_PULSE;
    int last_sent_pitch_pulse = PITCH_MID_PULSE;
    
    /* 板级初始化 (含调试串口 UART2) */
    board_init();
    
    printf("\r\n");
    printf("========================================\r\n");
    printf("  S300 IMU Stabilized Gimbal Demo\r\n");
    printf("========================================\r\n");
    printf("\r\n");
    
    /* 初始化 I2C3 (软件模拟) */
    printf("[Init] I2C%d at %d Hz...\r\n", IMU_I2C_IDX, IMU_I2C_FREQ);
    ret = i2c_soft_init_default_idx(&i2c, IMU_I2C_IDX, IMU_I2C_FREQ);
    if (ret != 0) {
        printf("[Error] I2C init failed!\r\n");
        while (1);
    }
    printf("[Init] I2C initialized.\r\n");
    
    /* 初始化 QMI8658A */
    printf("[Init] QMI8658A (addr=0x%02X)...\r\n", QMI8658A_ADDR);
    ret = qmi8658a_init(&imu_sensor, &i2c, QMI8658A_ADDR);
    if (ret != 0) {
        printf("[Error] QMI8658A init failed!\r\n");
        printf("        Please check I2C connection.\r\n");
        while (1);
    }
    printf("[Init] QMI8658A initialized.\r\n");
    
    /* 等待传感器稳定后读取温度 */
    delay_ms(100);
    temperature = qmi8658a_read_temperature(&imu_sensor);
    printf("[Info] Sensor temperature: %.2f C\r\n", temperature);
    
    /* 初始化总线舵机 (使用 board 统一接口) */
    printf("[Init] Bus Servo...\r\n");
    ret = board_servo_init(&servo);
    if (ret != 0) {
        printf("[Warning] Bus Servo init failed! Stabilization disabled.\r\n");
        stabilize_enabled = false;
    } else {
        printf("[Init] Bus Servo initialized.\r\n");
        
        /* 等待舵机上电 */
        delay_ms(500);
        
        /* 使能舵机 */
        bus_servo_set_load(&servo, SERVO_ROLL_ID, true);
        delay_ms(10);
        bus_servo_set_load(&servo, SERVO_PITCH_ID, true);
        delay_ms(10);
        bus_servo_set_load(&servo, 4, true);
        delay_ms(10);
        bus_servo_set_load(&servo, 5, true);
        delay_ms(10);
        bus_servo_set_load(&servo, 6, true);
        delay_ms(100);
        
        /* 舵机归中 (使用脉冲值) */
        printf("[Init] Moving servos to center position (pulse=%d)...\r\n", ROLL_MID_PULSE);
        bus_servo_move_raw(&servo, SERVO_ROLL_ID, ROLL_MID_PULSE, 500);
        delay_ms(10);
        bus_servo_move_raw(&servo, SERVO_PITCH_ID, PITCH_MID_PULSE, 500);
        delay_ms(10);
        /* 舵机4/5/6 初始位置 */
        bus_servo_move_raw(&servo, 4, 118, 500);
        delay_ms(10);
        bus_servo_move_raw(&servo, 5, 500, 500);
        delay_ms(10);
        bus_servo_move_raw(&servo, 6, 500, 500);
        delay_ms(600);
        printf("[Init] Servos centered.\r\n");
    }
    
    printf("\r\n");
    printf("[Info] Configuration:\r\n");
    printf("       - Sample rate: %.0f Hz\r\n", 1.0f / IMU_SAMPLE_DT);
    printf("       - Servo Roll ID: %d, Pitch ID: %d\r\n", SERVO_ROLL_ID, SERVO_PITCH_ID);
    printf("       - Target: Roll=%.1f deg, Pitch=%.1f deg\r\n", TARGET_ROLL, TARGET_PITCH);
    printf("       - Roll  PID: Kp=%.2f Ki=%.2f Kd=%.2f\r\n", ROLL_KP, ROLL_KI, ROLL_KD);
    printf("       - Pitch PID: Kp=%.2f Ki=%.2f Kd=%.2f\r\n", PITCH_KP, PITCH_KI, PITCH_KD);
    printf("       - LPF alpha: %.2f, Deadzone: %.1f deg\r\n", LPF_ALPHA, DEADZONE_DEG);
    printf("       - Stabilization: %s\r\n", stabilize_enabled ? "ENABLED" : "DISABLED");
    printf("\r\n");
    
#if TEST_MODE == 1
    /*=========================================================================
     * 轴向测试模式：测试舵机与IMU的对应关系
     *=========================================================================*/
    printf("========================================\r\n");
    printf("  AXIS MAPPING TEST MODE\r\n");
    printf("========================================\r\n\r\n");
    
    /* 测试位置：中心、左、右 */
    uint16_t test_positions[] = {500, 300, 700, 500};
    const char* pos_names[] = {"Center(500)", "Left(300)", "Right(700)", "Back to Center(500)"};
    
    printf("[Test] Testing Servo 3 (Roll axis candidate)...\r\n");
    printf("       Please observe which IMU axis changes.\r\n\r\n");
    
    for (int i = 0; i < 4; i++) {
        bus_servo_move_raw(&servo, 3, test_positions[i], 500);
        delay_ms(800);
        
        /* 读取多次取平均 */
        float sum_roll = 0, sum_pitch = 0;
        for (int j = 0; j < 10; j++) {
            qmi8658a_read_data(&imu_sensor, &sensor_data);
            calc_tilt_from_accel(sensor_data.acc, &tilt_y, &tilt_z);
            sum_roll += tilt_y;
            sum_pitch += tilt_z;
            delay_ms(20);
        }
        printf("[Servo3] Pos=%s -> Roll=%.1f, Pitch=%.1f\r\n", 
               pos_names[i], sum_roll/10.0f, sum_pitch/10.0f);
    }
    
    printf("\r\n[Test] Testing Servo 2 (Pitch axis candidate)...\r\n");
    printf("       Please observe which IMU axis changes.\r\n\r\n");
    
    for (int i = 0; i < 4; i++) {
        bus_servo_move_raw(&servo, 2, test_positions[i], 500);
        delay_ms(800);
        
        /* 读取多次取平均 */
        float sum_roll = 0, sum_pitch = 0;
        for (int j = 0; j < 10; j++) {
            qmi8658a_read_data(&imu_sensor, &sensor_data);
            calc_tilt_from_accel(sensor_data.acc, &tilt_y, &tilt_z);
            sum_roll += tilt_y;
            sum_pitch += tilt_z;
            delay_ms(20);
        }
        printf("[Servo2] Pos=%s -> Roll=%.1f, Pitch=%.1f\r\n", 
               pos_names[i], sum_roll/10.0f, sum_pitch/10.0f);
    }
    
    printf("\r\n========================================\r\n");
    printf("  TEST COMPLETE\r\n");
    printf("========================================\r\n");
    printf("\r\nAnalysis:\r\n");
    printf("- If Servo3 changes Roll significantly -> Servo3 controls Roll\r\n");
    printf("- If Servo3 changes Pitch significantly -> Servo3 controls Pitch\r\n");
    printf("- Check direction: Pulse increase should decrease angle error\r\n");
    printf("\r\nPlease set TEST_MODE=0 and adjust SERVO_ROLL_ID/PITCH_ID accordingly.\r\n");
    
    while(1) { delay_ms(1000); }  /* 停止 */
    
#endif /* TEST_MODE == 1 */

#if TEST_MODE == 2
    /*=========================================================================
     * IMU只读模式：舵机失能，只读取IMU数据
     * 用于确定期望的"垂直"状态对应的 Roll/Pitch 值
     *=========================================================================*/
    printf("========================================\r\n");
    printf("  IMU READ-ONLY MODE (Servos Disabled)\r\n");
    printf("========================================\r\n\r\n");
    
    /* 失能所有舵机 */
    printf("[Init] Disabling all servos...\r\n");
    bus_servo_set_load(&servo, 1, false);
    delay_ms(10);
    bus_servo_set_load(&servo, 2, false);
    delay_ms(10);
    bus_servo_set_load(&servo, 3, false);
    delay_ms(10);
    bus_servo_set_load(&servo, 4, false);
    delay_ms(10);
    bus_servo_set_load(&servo, 5, false);
    delay_ms(10);
    bus_servo_set_load(&servo, 6, false);
    delay_ms(100);
    printf("[Init] All servos disabled. You can move the gimbal freely.\r\n\r\n");
    
    printf("[Info] Please manually adjust the device to your desired VERTICAL position.\r\n");
    printf("[Info] Press any key or just observe the Roll/Pitch values.\r\n\r\n");
    
    /* 持续读取并打印IMU数据 */
    int read_count = 0;
    while(1) {
        ret = qmi8658a_read_data(&imu_sensor, &sensor_data);
        if (ret == 0) {
            calc_tilt_from_accel(sensor_data.acc, &tilt_y, &tilt_z);
            
            read_count++;
            if (read_count >= 10) {  /* 每100ms打印一次 */
                read_count = 0;
                printf("Roll:%+7.1f  Pitch:%+7.1f  | Acc: X=%+6.2f Y=%+6.2f Z=%+6.2f\r\n",
                       tilt_y, tilt_z,
                       sensor_data.acc[0], sensor_data.acc[1], sensor_data.acc[2]);
            }
        }
        delay_ms(10);
    }
    
#endif /* TEST_MODE == 2 */

    printf("[Info] Calibrating, please keep the device still...\r\n");;
    printf("\r\n");
    
    /* 主循环 */
    while (1) {
        /* 读取传感器数据 */
        ret = qmi8658a_read_data(&imu_sensor, &sensor_data);
        
        if (ret == 0) {
            /* 计算当前倾斜角度 (相对于垂直方向) */
            /* Roll=0, Pitch=0 表示 Z 轴朝上（垂直于地面） */
            calc_tilt_from_accel(sensor_data.acc, &tilt_y, &tilt_z);
            
            /* 等待传感器校准完成 */
            if (imu_sensor.calibrated && !zero_calibrated) {
                zero_cal_count++;
                if (zero_cal_count >= 50) {  /* 等待约0.5秒让传感器稳定 */
                    zero_calibrated = true;
                    printf("[Ready] Target: Keep Z-axis pointing UP (perpendicular to ground)\r\n");
                    printf("[Info] Roll=0, Pitch=0 means vertical. Servos will compensate tilt.\r\n\r\n");
                }
            }
            
            /* 舵机自稳控制 - 目标是让 Roll 趋向 TARGET_ROLL，Pitch 趋向 TARGET_PITCH */
            if (stabilize_enabled && zero_calibrated) {
                /* 当前角度（已通过加速度计计算） */
                float rel_roll = tilt_y;
                float rel_pitch = tilt_z;
                
                /* 低通滤波 */
                if (!filter_initialized) {
                    filtered_roll = rel_roll;
                    filtered_pitch = rel_pitch;
                    filter_initialized = true;
                } else {
                    filtered_roll = LPF_ALPHA * rel_roll + (1.0f - LPF_ALPHA) * filtered_roll;
                    filtered_pitch = LPF_ALPHA * rel_pitch + (1.0f - LPF_ALPHA) * filtered_pitch;
                }
                
                /* Roll PID 控制 - 补偿使 Roll 趋向于 TARGET_ROLL */
                float roll_error = filtered_roll - TARGET_ROLL;
                float roll_comp = 0.0f;
                
                /* 抗积分饱和：误差符号变化时衰减积分（穿越零点） */
                if ((roll_error > 0 && roll_integral < 0) || 
                    (roll_error < 0 && roll_integral > 0)) {
                    roll_integral *= 0.5f;  /* 快速衰减 */
                }
                
                /* 积分项始终累积（不在死区内清零），但死区内只用积分补偿 */
                roll_integral += roll_error * IMU_SAMPLE_DT;
                roll_integral = clamp(roll_integral, -INTEGRAL_MAX, INTEGRAL_MAX);
                float i_term = ROLL_KI * roll_integral;
                
                /* 微分项计算并滤波 */
                float roll_deriv_raw = (roll_error - roll_last_error) / IMU_SAMPLE_DT;
                roll_deriv_filtered = DERIV_LPF_ALPHA * roll_deriv_raw + (1.0f - DERIV_LPF_ALPHA) * roll_deriv_filtered;
                roll_last_error = roll_error;
                
                if (fabsf(roll_error) > DEADZONE_DEG) {
                    /* P */
                    float p_term = ROLL_KP * roll_error;
                    /* D - 使用滤波后的微分 */
                    float d_term = ROLL_KD * roll_deriv_filtered;
                    
                    /* 舵机补偿方向：反向补偿（舵机和传感器同向，需要取反） */
                    roll_comp = -(p_term + i_term + d_term);
                } else {
                    /* 死区内只用积分项保持位置，避免抖动 */
                    roll_comp = -i_term;
                }
                
                /* Pitch PID 控制 - 补偿使 Pitch 趋向于 TARGET_PITCH */
                float pitch_error = filtered_pitch - TARGET_PITCH;
                float pitch_comp = 0.0f;
                
                /* 抗积分饱和：误差符号变化时衰减积分 */
                if ((pitch_error > 0 && pitch_integral < 0) || 
                    (pitch_error < 0 && pitch_integral > 0)) {
                    pitch_integral *= 0.5f;
                }
                
                /* 积分项始终累积（不在死区内清零），但死区内只用积分补偿 */
                pitch_integral += pitch_error * IMU_SAMPLE_DT;
                pitch_integral = clamp(pitch_integral, -INTEGRAL_MAX, INTEGRAL_MAX);
                float pitch_i_term = PITCH_KI * pitch_integral;
                
                /* 微分项计算并滤波 */
                float pitch_deriv_raw = (pitch_error - pitch_last_error) / IMU_SAMPLE_DT;
                pitch_deriv_filtered = DERIV_LPF_ALPHA * pitch_deriv_raw + (1.0f - DERIV_LPF_ALPHA) * pitch_deriv_filtered;
                pitch_last_error = pitch_error;
                
                if (fabsf(pitch_error) > DEADZONE_DEG) {
                    float pitch_p_term = PITCH_KP * pitch_error;
                    /* D - 使用滤波后的微分 */
                    float pitch_d_term = PITCH_KD * pitch_deriv_filtered;
                    
                    /* 舵机补偿方向：反向补偿（与Roll相同） */
                    pitch_comp = -(pitch_p_term + pitch_i_term + pitch_d_term);
                } else {
                    /* 死区内只用积分项保持位置，避免抖动 */
                    pitch_comp = -pitch_i_term;
                }
                
                servo_roll_angle = clamp(roll_comp, ANGLE_MIN, ANGLE_MAX);
                servo_pitch_angle = clamp(pitch_comp, ANGLE_MIN, ANGLE_MAX);
                
                /* 速率限制 - 防止舵机突变 (渐进过渡) */
                float roll_delta = servo_roll_angle - last_roll_output;
                float pitch_delta = servo_pitch_angle - last_pitch_output;
                
                if (roll_delta > SERVO_RATE_LIMIT) roll_delta = SERVO_RATE_LIMIT;
                if (roll_delta < -SERVO_RATE_LIMIT) roll_delta = -SERVO_RATE_LIMIT;
                if (pitch_delta > SERVO_RATE_LIMIT) pitch_delta = SERVO_RATE_LIMIT;
                if (pitch_delta < -SERVO_RATE_LIMIT) pitch_delta = -SERVO_RATE_LIMIT;
                
                servo_roll_angle = last_roll_output + roll_delta;
                servo_pitch_angle = last_pitch_output + pitch_delta;
                
                last_roll_output = servo_roll_angle;
                last_pitch_output = servo_pitch_angle;
                
                /* 
                 * 第二级滤波：输出角度滤波 (借鉴 Aiming_Demo)
                 * 让舵机运动更丝滑
                 */
                filtered_out_roll = OUTPUT_LPF_ALPHA * servo_roll_angle + 
                                    (1.0f - OUTPUT_LPF_ALPHA) * filtered_out_roll;
                filtered_out_pitch = OUTPUT_LPF_ALPHA * servo_pitch_angle + 
                                     (1.0f - OUTPUT_LPF_ALPHA) * filtered_out_pitch;
                
                /* 将滤波后的角度转换为脉冲 */
                roll_pulse = (int)(ROLL_MID_PULSE + filtered_out_roll * PULSE_PER_DEGREE);
                pitch_pulse = (int)(PITCH_MID_PULSE + filtered_out_pitch * PULSE_PER_DEGREE);
                
                /* 限制脉冲范围 */
                roll_pulse = (roll_pulse < ROLL_MIN_PULSE) ? ROLL_MIN_PULSE : 
                             (roll_pulse > ROLL_MAX_PULSE) ? ROLL_MAX_PULSE : roll_pulse;
                pitch_pulse = (pitch_pulse < PITCH_MIN_PULSE) ? PITCH_MIN_PULSE : 
                              (pitch_pulse > PITCH_MAX_PULSE) ? PITCH_MAX_PULSE : pitch_pulse;
                
                /* 
                 * 最小移动阈值 (借鉴 Tracking_Demo)
                 * 脉冲变化太小时不发送指令，减少抖动
                 */
                bool should_move_roll = (abs(roll_pulse - last_sent_roll_pulse) >= 
                                        (int)(MIN_MOVE_THRESHOLD * PULSE_PER_DEGREE));
                bool should_move_pitch = (abs(pitch_pulse - last_sent_pitch_pulse) >= 
                                         (int)(MIN_MOVE_THRESHOLD * PULSE_PER_DEGREE));
                
                if (should_move_roll) {
                    bus_servo_move_raw(&servo, SERVO_ROLL_ID, (uint16_t)roll_pulse, SERVO_MOVE_TIME);
                    last_sent_roll_pulse = roll_pulse;
                    delay_ms(5);
                }
                if (should_move_pitch) {
                    bus_servo_move_raw(&servo, SERVO_PITCH_ID, (uint16_t)pitch_pulse, SERVO_MOVE_TIME);
                    last_sent_pitch_pulse = pitch_pulse;
                }
            }
            
            /* 定期打印 */
            sample_count++;
            if (sample_count >= PRINT_INTERVAL) {
                sample_count = 0;
                
                /* 检查校准状态 */
                if (!imu_sensor.calibrated) {
                    printf("[Sensor Cal] ");
                } else if (!zero_calibrated) {
                    printf("[Waiting %d/50] ", zero_cal_count);
                }
                
                /* 打印倾斜角度和舵机状态 */
                /* Roll=0, Pitch=0 表示垂直于地面 */
                float disp_roll = zero_calibrated ? filtered_roll : tilt_y;
                float disp_pitch = zero_calibrated ? filtered_pitch : tilt_z;
                printf("Roll:%+5.1f Pitch:%+5.1f | Servo: R=%+5.1f(%3d) P=%+5.1f(%3d)\r\n",
                       disp_roll, disp_pitch,
                       servo_roll_angle, roll_pulse, servo_pitch_angle, pitch_pulse);
                
                /* 调试信息：追踪Roll控制链 */
                if (zero_calibrated && stabilize_enabled) {
                    float dbg_err = filtered_roll - TARGET_ROLL;
                    printf("  [DBG] err=%+.2f int=%+.1f comp=%+.2f filt_out=%+.2f sent=%d\r\n",
                           dbg_err, roll_integral, 
                           last_roll_output, filtered_out_roll, last_sent_roll_pulse);
                }
            }
        }
        
        /* 采样延时 */
        delay_ms(IMU_SAMPLE_MS);
    }
    
    return 0;
}
