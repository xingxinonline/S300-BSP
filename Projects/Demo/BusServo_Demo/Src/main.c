/**
 * @file    main.c
 * @brief   两轴云台控制演示程序 (Two-Axis Gimbal Demo)
 * @details 使用 LeArm 机械臂的 ID=6 (Yaw) 和 ID=4 (Pitch) 舵机实现两轴控制:
 *          - ID=6: 底座/偏航 (Yaw) - 左右旋转, 角度 -90°~+90°, 脉冲 125~875
 *          - ID=4: 肘部/俯仰 (Pitch) - 上下点头, 角度 -90°~0°, 脉冲 125~500
 * 
 * @note    硬件要求:
 *          - gimbal_master 板
 *          - LeArm 6DOF 机械臂 (只使用 ID=4 和 ID=6)
 *          - 舵机供电 5V~8.4V
 */

#include "s300.h"
#include "board.h"
#include "bus_servo.h"
#include "uart.h"
#include "gpio.h"
#include "rcc.h"
#include <stdio.h>
#include <math.h>

/*===========================================================================
 * 两轴云台配置
 *===========================================================================*/

/** @brief Yaw 舵机 ID (底座旋转) */
#define YAW_SERVO_ID        6

/** @brief Pitch 舵机 ID (肘部俯仰) */
#define PITCH_SERVO_ID      4

/*---------------------------------------------------------------------------
 * Yaw 舵机参数: -90° ~ +90°
 *---------------------------------------------------------------------------*/
#define YAW_MIN_PULSE       125     /* -90° */
#define YAW_MID_PULSE       500     /*   0° */
#define YAW_MAX_PULSE       875     /* +90° */
#define YAW_MIN_ANGLE       (-90.0f)
#define YAW_MAX_ANGLE       (90.0f)

/*---------------------------------------------------------------------------
 * Pitch 舵机参数: -90° ~ 0°
 *---------------------------------------------------------------------------*/
#define PITCH_MIN_PULSE     125     /* -90° */
#define PITCH_MID_PULSE     312     /* -45° (默认位置) */
#define PITCH_MAX_PULSE     500     /*   0° */
#define PITCH_MIN_ANGLE     (-90.0f)
#define PITCH_MAX_ANGLE     (0.0f)

/** @brief 默认运动时间 (ms) */
#define DEFAULT_MOVE_TIME   500

/** @brief 演示循环延时 (ms) */
#define DEMO_DELAY_MS       1500

/*===========================================================================
 * Private Functions
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
 * @brief Yaw 脉冲转角度
 */
static float yaw_pulse_to_angle(int pulse)
{
    /* 线性映射: 125~875 -> -90~+90 */
    return YAW_MIN_ANGLE + (float)(pulse - YAW_MIN_PULSE) * 
           (YAW_MAX_ANGLE - YAW_MIN_ANGLE) / (float)(YAW_MAX_PULSE - YAW_MIN_PULSE);
}

/**
 * @brief Yaw 角度转脉冲
 */
static int yaw_angle_to_pulse(float angle)
{
    /* 限制范围 */
    if (angle < YAW_MIN_ANGLE) angle = YAW_MIN_ANGLE;
    if (angle > YAW_MAX_ANGLE) angle = YAW_MAX_ANGLE;
    
    int pulse = YAW_MIN_PULSE + (int)((angle - YAW_MIN_ANGLE) * 
                (float)(YAW_MAX_PULSE - YAW_MIN_PULSE) / (YAW_MAX_ANGLE - YAW_MIN_ANGLE));
    return pulse;
}

/**
 * @brief Pitch 脉冲转角度
 */
static float pitch_pulse_to_angle(int pulse)
{
    /* 线性映射: 125~500 -> -90~0 */
    return PITCH_MIN_ANGLE + (float)(pulse - PITCH_MIN_PULSE) * 
           (PITCH_MAX_ANGLE - PITCH_MIN_ANGLE) / (float)(PITCH_MAX_PULSE - PITCH_MIN_PULSE);
}

/**
 * @brief Pitch 角度转脉冲
 */
static int pitch_angle_to_pulse(float angle)
{
    /* 限制范围 */
    if (angle < PITCH_MIN_ANGLE) angle = PITCH_MIN_ANGLE;
    if (angle > PITCH_MAX_ANGLE) angle = PITCH_MAX_ANGLE;
    
    int pulse = PITCH_MIN_PULSE + (int)((angle - PITCH_MIN_ANGLE) * 
                (float)(PITCH_MAX_PULSE - PITCH_MIN_PULSE) / (PITCH_MAX_ANGLE - PITCH_MIN_ANGLE));
    return pulse;
}

/**
 * @brief 移动云台到指定位置 (脉冲值)
 */
static void gimbal_move_pulse(bus_servo_t *servo, int yaw_pulse, int pitch_pulse, uint16_t time_ms)
{
        printf("[Gimbal] Yaw=%d (%.1fdeg), Pitch=%d (%.1fdeg), Time=%dms\r\n",
            yaw_pulse, yaw_pulse_to_angle(yaw_pulse),
            pitch_pulse, pitch_pulse_to_angle(pitch_pulse),
            time_ms);
    
    bus_servo_move_raw(servo, YAW_SERVO_ID, (uint16_t)yaw_pulse, time_ms);
    delay_ms(5);  /* 舵机间延时 */
    bus_servo_move_raw(servo, PITCH_SERVO_ID, (uint16_t)pitch_pulse, time_ms);
}

/**
 * @brief 移动云台到指定位置 (角度值)
 */
static void gimbal_move_angle(bus_servo_t *servo, float yaw_angle, float pitch_angle, uint16_t time_ms)
{
    int yaw_pulse = yaw_angle_to_pulse(yaw_angle);
    int pitch_pulse = pitch_angle_to_pulse(pitch_angle);
    gimbal_move_pulse(servo, yaw_pulse, pitch_pulse, time_ms);
}

/**
 * @brief 云台归中 (Yaw=0°, Pitch=-45°)
 */
static void gimbal_center(bus_servo_t *servo, uint16_t time_ms)
{
    printf("[Gimbal] Centering (Yaw=0deg, Pitch=-45deg)...\r\n");
    gimbal_move_pulse(servo, YAW_MID_PULSE, PITCH_MID_PULSE, time_ms);
}

/**
 * @brief 读取云台当前位置
 */
static void gimbal_read_position(bus_servo_t *servo)
{
    int16_t yaw_pos, pitch_pos;
    int ret;
    
    ret = bus_servo_read_position(servo, YAW_SERVO_ID, &yaw_pos);
    if (ret == 0) {
        printf("[Status] Yaw  (ID=%d): %d (%.1fdeg)\r\n", 
               YAW_SERVO_ID, yaw_pos, yaw_pulse_to_angle(yaw_pos));
    } else {
        printf("[Error] Yaw read failed (err=%d)\r\n", ret);
    }
    
    delay_ms(5);  /* 命令间延时 */
    
    ret = bus_servo_read_position(servo, PITCH_SERVO_ID, &pitch_pos);
    if (ret == 0) {
        printf("[Status] Pitch(ID=%d): %d (%.1fdeg)\r\n", 
               PITCH_SERVO_ID, pitch_pos, pitch_pulse_to_angle(pitch_pos));
    } else {
        printf("[Error] Pitch read failed (err=%d)\r\n", ret);
    }
}

/**
 * @brief 读取并打印舵机完整信息
 */
static void gimbal_print_servo_info(bus_servo_t *servo, uint8_t id)
{
    int16_t pos;
    uint16_t vin;
    uint8_t temp, load;
    
    printf("\r\n====== Servo ID=%d Info ======\r\n", id);
    
    if (bus_servo_read_position(servo, id, &pos) == 0) {
        float angle = (id == YAW_SERVO_ID) ? yaw_pulse_to_angle(pos) : pitch_pulse_to_angle(pos);
        printf("  Position:    %d (%.1f deg)\r\n", pos, angle);
    } else {
        printf("  Position:    [READ FAILED]\r\n");
    }
    delay_ms(5);
    
    if (bus_servo_read_temp(servo, id, &temp) == 0) {
        printf("  Temperature: %d C\r\n", temp);
    } else {
        printf("  Temperature: [READ FAILED]\r\n");
    }
    delay_ms(5);
    
    if (bus_servo_read_vin(servo, id, &vin) == 0) {
        printf("  Voltage:     %d mV (%.2f V)\r\n", vin, vin / 1000.0f);
    } else {
        printf("  Voltage:     [READ FAILED]\r\n");
    }
    delay_ms(5);
    
    if (bus_servo_read_load(servo, id, &load) == 0) {
        printf("  Load:        %s\r\n", load ? "ENABLED" : "DISABLED");
    } else {
        printf("  Load:        [READ FAILED]\r\n");
    }
    
    printf("==============================\r\n");
}

/**
 * @brief 扫描演示 - 水平扫描 (Yaw: -90° ~ +90°)
 */
static void demo_horizontal_scan(bus_servo_t *servo)
{
    printf("\r\n=== Horizontal Scan Demo ===\r\n");
    
    /* 从左到右 */
    for (int yaw = YAW_MIN_PULSE; yaw <= YAW_MAX_PULSE; yaw += 50) {
        gimbal_move_pulse(servo, yaw, PITCH_MID_PULSE, 100);
        delay_ms(150);
    }
    
    /* 从右到左 */
    for (int yaw = YAW_MAX_PULSE; yaw >= YAW_MIN_PULSE; yaw -= 50) {
        gimbal_move_pulse(servo, yaw, PITCH_MID_PULSE, 100);
        delay_ms(150);
    }
    
    /* 归中 */
    gimbal_center(servo, 300);
}

/**
 * @brief 扫描演示 - 垂直扫描 (Pitch: -90° ~ 0°)
 */
static void demo_vertical_scan(bus_servo_t *servo)
{
    printf("\r\n=== Vertical Scan Demo ===\r\n");
    
    /* 从下到上 (-90° -> 0°) */
    for (int pitch = PITCH_MIN_PULSE; pitch <= PITCH_MAX_PULSE; pitch += 25) {
        gimbal_move_pulse(servo, YAW_MID_PULSE, pitch, 100);
        delay_ms(150);
    }
    
    /* 从上到下 (0° -> -90°) */
    for (int pitch = PITCH_MAX_PULSE; pitch >= PITCH_MIN_PULSE; pitch -= 25) {
        gimbal_move_pulse(servo, YAW_MID_PULSE, pitch, 100);
        delay_ms(150);
    }
    
    /* 归中 */
    gimbal_center(servo, 300);
}

/**
 * @brief 半圆扫描演示 (适合 Pitch 只有 -90°~0° 的情况)
 */
static void demo_semicircle_motion(bus_servo_t *servo)
{
    printf("\r\n=== Semicircle Motion Demo ===\r\n");
    
    const int steps = 16;  /* 分段数 */
    
    /* 画半圆近似 (Yaw: -60° ~ +60°, Pitch: -30° -> -60° -> -30°)
     * 使用二次曲线近似避免链接 math 库 (cos/sin)
     */
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;  /* 0 ~ 1 */

        /* 线性 yaw 从 -60 到 +60 */
        float yaw = -60.0f + 120.0f * t;

        /* 二次曲线使 pitch 在中点下降到 -60°, 两端为 -30° */
        float x = 2.0f * t - 1.0f; /* -1 ~ 1 */
        float pitch = -30.0f - 30.0f * (1.0f - x * x);

        gimbal_move_angle(servo, yaw, pitch, 80);
        delay_ms(100);
    }
    
    /* 归中 */
    gimbal_center(servo, 300);
}

/**
 * @brief 预设位置演示 (适配 Pitch: -90°~0°)
 */
static void demo_preset_positions(bus_servo_t *servo)
{
    printf("\r\n=== Preset Positions Demo ===\r\n");
    
    /* 定义预设位置: {yaw, pitch} 角度 */
    /* Pitch 范围: -90°~0°, 所以用 -45° 作为中间, 0° 为上, -90° 为下 */
    const float presets[][2] = {
        {   0.0f, -45.0f },  /* 中心 */
        { -60.0f, -45.0f },  /* 左 */
        {  60.0f, -45.0f },  /* 右 */
        {   0.0f,   0.0f },  /* 上 (水平) */
        {   0.0f, -90.0f },  /* 下 (垂直向下) */
        { -45.0f,   0.0f },  /* 左上 */
        {  45.0f,   0.0f },  /* 右上 */
        { -45.0f, -70.0f },  /* 左下 */
        {  45.0f, -70.0f },  /* 右下 */
        {   0.0f, -45.0f },  /* 回中心 */
    };
    const char *names[] = {
        "Center", "Left", "Right", "Up", "Down",
        "Left-Up", "Right-Up", "Left-Down", "Right-Down", "Center"
    };
    
    int num_presets = sizeof(presets) / sizeof(presets[0]);
    
    for (int i = 0; i < num_presets; i++) {
        printf("[Preset] %s (Yaw=%.0fdeg, Pitch=%.0fdeg)\r\n", 
               names[i], presets[i][0], presets[i][1]);
        gimbal_move_angle(servo, presets[i][0], presets[i][1], DEFAULT_MOVE_TIME);
        delay_ms(DEFAULT_MOVE_TIME + 500);
    }
}

/*===========================================================================
 * Main Function
 *===========================================================================*/

int main(void)
{
    bus_servo_t servo;
    
    /* 初始化 DWT 用于微秒级延时 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
    
    /* 板级初始化 */
    board_init();
    
    printf("\r\n");
    printf("========================================\r\n");
    printf("  Two-Axis Gimbal Demo - S300 BSP\r\n");
    printf("  Yaw:   ID=%d, Pulse %d~%d, Angle -90~+90\r\n", 
           YAW_SERVO_ID, YAW_MIN_PULSE, YAW_MAX_PULSE);
    printf("  Pitch: ID=%d, Pulse %d~%d, Angle -90~0\r\n", 
           PITCH_SERVO_ID, PITCH_MIN_PULSE, PITCH_MAX_PULSE);
    printf("========================================\r\n\r\n");
    
    /* 初始化总线舵机控制器 */
    printf("[Init] Initializing bus servo controller...\r\n");
    int ret = board_servo_init(&servo);
    if (ret != 0) {
        printf("[Error] Failed to init servo controller (err=%d)\r\n", ret);
        while (1);
    }
    printf("[OK] Servo controller initialized\r\n");
    
    /* 延时等待舵机上电 */
    printf("[Info] Waiting for servo power up...\r\n");
    delay_ms(500);
    
    /* 装载电机 */
    printf("[Info] Loading motors...\r\n");
    bus_servo_set_load(&servo, YAW_SERVO_ID, true);
    delay_ms(10);
    bus_servo_set_load(&servo, PITCH_SERVO_ID, true);
    delay_ms(100);
    printf("[OK] Motors loaded\r\n");
    
    /* 显示舵机状态 */
    printf("\r\n===== Servo Status =====\r\n");
    gimbal_print_servo_info(&servo, YAW_SERVO_ID);
    gimbal_print_servo_info(&servo, PITCH_SERVO_ID);
    
    /* 归中到默认位置 (Yaw=0°, Pitch=-45°) */
    printf("[Info] Centering gimbal (Yaw=0, Pitch=-45)...\r\n");
    gimbal_center(&servo, 1000);
    delay_ms(1200);
    
    /* 读取初始位置 */
    printf("\r\n[Info] Initial position:\r\n");
    gimbal_read_position(&servo);
    
    /* 主循环 */
    uint32_t loop_count = 0;
    while (1) {
        printf("\r\n========== Gimbal Demo Loop %lu ==========\r\n", ++loop_count);
        
        /* 1. 预设位置演示 */
        demo_preset_positions(&servo);
        delay_ms(DEMO_DELAY_MS);
        
        /* 2. 水平扫描演示 */
        demo_horizontal_scan(&servo);
        delay_ms(DEMO_DELAY_MS);
        
        /* 3. 垂直扫描演示 */
        demo_vertical_scan(&servo);
        delay_ms(DEMO_DELAY_MS);
        
        /* 4. 半圆扫描演示 */
        demo_semicircle_motion(&servo);
        delay_ms(DEMO_DELAY_MS);
        
        /* 读取最终位置 */
        printf("\r\n[Info] Current position:\r\n");
        gimbal_read_position(&servo);
        
        printf("\r\n[Info] Demo cycle complete. Waiting...\r\n");
        delay_ms(3000);
    }
    
    return 0;
}
