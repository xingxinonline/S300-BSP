/**
 * @file    gimbal_ctrl.c
 * @brief   云台控制模块实现
 */

#include "gimbal_ctrl.h"
#include "bus_servo.h"
#include "board.h"
#include "s300.h"  /* for CMSIS definitions */
#include <stdio.h>

/*===========================================================================
 * 舵机脉冲参数
 *===========================================================================*/

/*
 * 舵机脉冲换算：0-1000 对应 240°，中点 500 = 0°
 * 每度 = 1000/240 ≈ 4.167 pulse
 */
#define YAW_MIN_PULSE       125     /* -90° */
#define YAW_MID_PULSE       500     /*   0° */
#define YAW_MAX_PULSE       875     /* +90° */

#define PITCH_MIN_PULSE     250     /* -60°: 500 + (-60)*4.167 ≈ 250 */
#define PITCH_MID_PULSE     312     /* -45°: 500 + (-45)*4.167 ≈ 312 */
#define PITCH_MAX_PULSE     375     /* -30°: 500 + (-30)*4.167 ≈ 375 */

/*===========================================================================
 * 私有变量
 *===========================================================================*/

static bus_servo_t s_servo;
static float s_yaw_deg = 0.0f;
static float s_pitch_deg = -45.0f;
static uint32_t (*s_get_millis)(void) = NULL;

/*===========================================================================
 * 辅助函数
 *===========================================================================*/

static int yaw_deg_to_pulse(float deg)
{
    if (deg < GIMBAL_YAW_MIN) deg = GIMBAL_YAW_MIN;
    if (deg > GIMBAL_YAW_MAX) deg = GIMBAL_YAW_MAX;
    return YAW_MIN_PULSE + (int)((deg - GIMBAL_YAW_MIN) * 
           (float)(YAW_MAX_PULSE - YAW_MIN_PULSE) / (GIMBAL_YAW_MAX - GIMBAL_YAW_MIN));
}

static float yaw_pulse_to_deg(int pulse)
{
    return GIMBAL_YAW_MIN + (float)(pulse - YAW_MIN_PULSE) * 
           (GIMBAL_YAW_MAX - GIMBAL_YAW_MIN) / (float)(YAW_MAX_PULSE - YAW_MIN_PULSE);
}

static int pitch_deg_to_pulse(float deg)
{
    if (deg < GIMBAL_PITCH_MIN) deg = GIMBAL_PITCH_MIN;
    if (deg > GIMBAL_PITCH_MAX) deg = GIMBAL_PITCH_MAX;
    return PITCH_MIN_PULSE + (int)((deg - GIMBAL_PITCH_MIN) * 
           (float)(PITCH_MAX_PULSE - PITCH_MIN_PULSE) / (GIMBAL_PITCH_MAX - GIMBAL_PITCH_MIN));
}

static float pitch_pulse_to_deg(int pulse)
{
    return GIMBAL_PITCH_MIN + (float)(pulse - PITCH_MIN_PULSE) * 
           (GIMBAL_PITCH_MAX - GIMBAL_PITCH_MIN) / (float)(PITCH_MAX_PULSE - PITCH_MIN_PULSE);
}

/**
 * @brief 使用 millis 回调实现延时 (不干扰 SysTick)
 */
static void delay_ms(uint32_t ms)
{
    if (s_get_millis == NULL) {
        /* Fallback: 简单循环延时 */
        volatile uint32_t count = ms * (SystemCoreClock / 10000);
        while (count--) { __NOP(); }
        return;
    }
    uint32_t start = s_get_millis();
    while ((s_get_millis() - start) < ms);
}

/*===========================================================================
 * API 实现
 *===========================================================================*/

int gimbal_init(uint32_t (*get_millis)(void))
{
    /* 保存时间回调 */
    s_get_millis = get_millis;
    
    /* 初始化舵机 */
    int ret = board_servo_init(&s_servo);
    if (ret != 0) {
        printf("[Gimbal] Init failed (err=%d)\r\n", ret);
        return ret;
    }
    
    /* 等待舵机上电 */
    delay_ms(500);
    
    /* 使能并初始化未使用的舵机 (ID 2, 3, 5)，防止抖动 */
    /* 设置到中间位置 (500) 并使能 */
    bus_servo_set_load(&s_servo, 2, true);
    delay_ms(10);
    bus_servo_move_raw(&s_servo, 2, 500, 1000);
    delay_ms(10);
    
    bus_servo_set_load(&s_servo, 3, true);
    delay_ms(10);
    bus_servo_move_raw(&s_servo, 3, 500, 1000);
    delay_ms(10);
    
    bus_servo_set_load(&s_servo, 5, true);
    delay_ms(10);
    bus_servo_move_raw(&s_servo, 5, 500, 1000);
    delay_ms(10);
    
    /* 使能云台舵机 (ID 4, 6) */
    bus_servo_set_load(&s_servo, GIMBAL_YAW_ID, true);
    delay_ms(10);
    bus_servo_set_load(&s_servo, GIMBAL_PITCH_ID, true);
    delay_ms(100);
    
    printf("[Gimbal] Initialized\r\n");
    return 0;
}

void gimbal_center(void)
{
    s_yaw_deg = 0.0f;
    s_pitch_deg = -45.0f;
    
    bus_servo_move_raw(&s_servo, GIMBAL_YAW_ID, YAW_MID_PULSE, 1000);
    delay_ms(5);
    bus_servo_move_raw(&s_servo, GIMBAL_PITCH_ID, PITCH_MID_PULSE, 1000);
}

void gimbal_move(float yaw_deg, float pitch_deg, uint16_t time_ms)
{
    /* 限制范围 */
    if (yaw_deg < GIMBAL_YAW_MIN) yaw_deg = GIMBAL_YAW_MIN;
    if (yaw_deg > GIMBAL_YAW_MAX) yaw_deg = GIMBAL_YAW_MAX;
    if (pitch_deg < GIMBAL_PITCH_MIN) pitch_deg = GIMBAL_PITCH_MIN;
    if (pitch_deg > GIMBAL_PITCH_MAX) pitch_deg = GIMBAL_PITCH_MAX;
    
    s_yaw_deg = yaw_deg;
    s_pitch_deg = pitch_deg;
    
    int yaw_pulse = yaw_deg_to_pulse(yaw_deg);
    int pitch_pulse = pitch_deg_to_pulse(pitch_deg);
    
    /* DEBUG: 打印云台命令 */
    static uint32_t s_cmd_count = 0;
    s_cmd_count++;
    if ((s_cmd_count % 20) == 1) {  /* 每 20 次打印一次 */
        printf("[Gimbal] CMD#%lu: yaw=%.1f(%d) pitch=%.1f(%d)\r\n",
               (unsigned long)s_cmd_count, yaw_deg, yaw_pulse, pitch_deg, pitch_pulse);
    }
    
    bus_servo_move_raw(&s_servo, GIMBAL_YAW_ID, (uint16_t)yaw_pulse, time_ms);
    delay_ms(5);
    bus_servo_move_raw(&s_servo, GIMBAL_PITCH_ID, (uint16_t)pitch_pulse, time_ms);
}

int gimbal_get_position(float *yaw_deg, float *pitch_deg)
{
    int16_t yaw_pulse, pitch_pulse;
    int ret;
    
    ret = bus_servo_read_position(&s_servo, GIMBAL_YAW_ID, &yaw_pulse);
    if (ret != 0) return ret;
    
    delay_ms(5);
    
    ret = bus_servo_read_position(&s_servo, GIMBAL_PITCH_ID, &pitch_pulse);
    if (ret != 0) return ret;
    
    if (yaw_deg) *yaw_deg = yaw_pulse_to_deg(yaw_pulse);
    if (pitch_deg) *pitch_deg = pitch_pulse_to_deg(pitch_pulse);
    
    return 0;
}

void gimbal_move_delta(float delta_yaw, float delta_pitch, uint16_t time_ms)
{
    float new_yaw = s_yaw_deg + delta_yaw;
    float new_pitch = s_pitch_deg + delta_pitch;
    gimbal_move(new_yaw, new_pitch, time_ms);
}
