/**
 * @file    target_tracker.c
 * @brief   目标跟踪模块实现 (PID 控制)
 */

#include "target_tracker.h"
#include "gimbal_ctrl.h"
#include <stdio.h>
#include <math.h>

/*===========================================================================
 * 配置参数
 *===========================================================================*/

/** @brief 控制周期 (ms) - 与运动时间匹配 */
#define CONTROL_PERIOD_MS       30
#define MOVE_TIME_MS            CONTROL_PERIOD_MS

/** @brief 目标丢失超时 (ms) */
#define TARGET_LOST_TIMEOUT_MS  500

/** @brief 默认死区 (像素) */
#define DEFAULT_DEADZONE        15

/** 
 * @brief X/Y 轴分离的 PID 参数
 * Pitch(Y) 使用更保守的参数，减少上下抖动
 */
#define DEFAULT_KP_X            0.08f
#define DEFAULT_KI_X            0.002f
#define DEFAULT_KD_X            0.02f

#define DEFAULT_KP_Y            0.06f   /* Pitch 更保守 */
#define DEFAULT_KI_Y            0.001f
#define DEFAULT_KD_Y            0.015f

/** @brief 积分限幅 */
#define INTEGRAL_MAX            50.0f

/** @brief 输出限幅 (度/次) */
#define OUTPUT_MAX_X            10.0f
#define OUTPUT_MAX_Y            6.0f    /* Pitch 限幅更小 */

/** @brief 最小运动阈值 (度) */
#define MIN_MOVE_THRESHOLD      0.5f

/**
 * @brief 目标位置低通滤波系数 (0~1)
 * 平滑检测框抖动，越小越平滑
 */
#define FILTER_ALPHA_X          0.4f
#define FILTER_ALPHA_Y          0.25f   /* Y 更强滤波 */

/**
 * @brief 渐进启动：最大输出速度 (度/周期)
 * 防止首次跟踪时云台突然运动
 */
#define MAX_MOVE_SPEED_YAW      5.0f
#define MAX_MOVE_SPEED_PITCH    4.0f

/*===========================================================================
 * 私有变量
 *===========================================================================*/

static uint32_t (*s_get_millis)(void) = NULL;
static int s_img_width = 320;
static int s_img_height = 240;
static int s_img_cx, s_img_cy;

/* 跟踪状态 */
static bool s_enabled = true;
static bool s_tracking = false;
static uint32_t s_last_target_time = 0;
static uint32_t s_last_control_time = 0;

/* 当前目标 (原始值) */
static int s_target_cx = 0;
static int s_target_cy = 0;
static bool s_target_valid = false;

/* 滤波后的目标位置 */
static float s_filtered_cx = 0.0f;
static float s_filtered_cy = 0.0f;
static bool s_filter_initialized = false;

/* X/Y 分离的 PID 参数 */
static float s_kp_x = DEFAULT_KP_X;
static float s_ki_x = DEFAULT_KI_X;
static float s_kd_x = DEFAULT_KD_X;
static float s_kp_y = DEFAULT_KP_Y;
static float s_ki_y = DEFAULT_KI_Y;
static float s_kd_y = DEFAULT_KD_Y;

/* PID 状态 */
static float s_error_x_sum = 0.0f;
static float s_error_y_sum = 0.0f;
static float s_error_x_prev = 0.0f;
static float s_error_y_prev = 0.0f;

/* 上次输出 (用于渐进启动) */
static float s_last_delta_yaw = 0.0f;
static float s_last_delta_pitch = 0.0f;
static bool s_first_move = true;

/* 死区 */
static int s_deadzone = DEFAULT_DEADZONE;

/*===========================================================================
 * 辅助函数
 *===========================================================================*/

static inline uint32_t millis(void)
{
    return s_get_millis ? s_get_millis() : 0;
}

static float clamp(float val, float min_val, float max_val)
{
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

static float fabs_f(float x)
{
    return (x >= 0) ? x : -x;
}

/*===========================================================================
 * API 实现
 *===========================================================================*/

void tracker_init(uint32_t (*get_millis)(void), int img_width, int img_height)
{
    s_get_millis = get_millis;
    s_img_width = img_width;
    s_img_height = img_height;
    s_img_cx = img_width / 2;
    s_img_cy = img_height / 2;
    
    s_enabled = true;
    s_tracking = false;
    s_target_valid = false;
    s_filter_initialized = false;
    s_first_move = true;
    
    /* 重置 PID */
    s_error_x_sum = 0;
    s_error_y_sum = 0;
    s_error_x_prev = 0;
    s_error_y_prev = 0;
    
    /* 重置渐进启动 */
    s_last_delta_yaw = 0;
    s_last_delta_pitch = 0;
    
    printf("[Tracker] Init: image %dx%d, center (%d,%d)\r\n",
           img_width, img_height, s_img_cx, s_img_cy);
    printf("[Tracker] PID X: Kp=%.3f, Ki=%.3f, Kd=%.3f\r\n", s_kp_x, s_ki_x, s_kd_x);
    printf("[Tracker] PID Y: Kp=%.3f, Ki=%.3f, Kd=%.3f\r\n", s_kp_y, s_ki_y, s_kd_y);
    printf("[Tracker] Filter: X=%.2f, Y=%.2f\r\n", FILTER_ALPHA_X, FILTER_ALPHA_Y);
}

void tracker_update_target(const tracker_target_t *target)
{
    if (!target || !target->valid) {
        s_target_valid = false;
        return;
    }
    
    /* 计算目标中心 */
    s_target_cx = (target->x1 + target->x2) / 2;
    s_target_cy = (target->y1 + target->y2) / 2;
    s_target_valid = true;
    s_last_target_time = millis();
    
    if (!s_tracking) {
        s_tracking = true;
        s_first_move = true;  /* 标记首次移动 */
        
        /* 初始化滤波器 */
        s_filtered_cx = (float)s_target_cx;
        s_filtered_cy = (float)s_target_cy;
        s_filter_initialized = true;
        
        printf("[Tracker] Target acquired at (%d,%d)\r\n", s_target_cx, s_target_cy);
    }
}

void tracker_poll(void)
{
    if (!s_enabled) return;
    
    uint32_t now = millis();
    
    /* 检查目标超时 */
    if (s_target_valid && (now - s_last_target_time > TARGET_LOST_TIMEOUT_MS)) {
        s_target_valid = false;
        if (s_tracking) {
            s_tracking = false;
            s_filter_initialized = false;
            s_first_move = true;
            printf("[Tracker] Target lost\r\n");
            /* 重置 PID 积分 */
            s_error_x_sum = 0;
            s_error_y_sum = 0;
        }
    }
    
    /* 控制周期检查 */
    if (now - s_last_control_time < CONTROL_PERIOD_MS) return;
    s_last_control_time = now;
    
    if (!s_target_valid || !s_tracking) return;
    
    /* 
     * 目标位置低通滤波 - 减少检测框抖动
     */
    if (s_filter_initialized) {
        s_filtered_cx = FILTER_ALPHA_X * (float)s_target_cx + 
                        (1.0f - FILTER_ALPHA_X) * s_filtered_cx;
        s_filtered_cy = FILTER_ALPHA_Y * (float)s_target_cy + 
                        (1.0f - FILTER_ALPHA_Y) * s_filtered_cy;
    }
    
    /* 计算误差 (滤波后的目标相对图像中心的偏移) */
    float error_x = s_filtered_cx - (float)s_img_cx;
    float error_y = s_filtered_cy - (float)s_img_cy;
    
    /* 死区处理 */
    if (fabs_f(error_x) < s_deadzone) error_x = 0;
    if (fabs_f(error_y) < s_deadzone) error_y = 0;
    
    if (error_x == 0 && error_y == 0) return;
    
    /* PID 计算 - X/Y 轴分离参数 */
    /* 积分 */
    s_error_x_sum += error_x;
    s_error_y_sum += error_y;
    s_error_x_sum = clamp(s_error_x_sum, -INTEGRAL_MAX, INTEGRAL_MAX);
    s_error_y_sum = clamp(s_error_y_sum, -INTEGRAL_MAX, INTEGRAL_MAX);
    
    /* 微分 */
    float d_error_x = error_x - s_error_x_prev;
    float d_error_y = error_y - s_error_y_prev;
    s_error_x_prev = error_x;
    s_error_y_prev = error_y;
    
    /* 输出 - 使用分离的 PID 参数 */
    float out_x = s_kp_x * error_x + s_ki_x * s_error_x_sum + s_kd_x * d_error_x;
    float out_y = s_kp_y * error_y + s_ki_y * s_error_y_sum + s_kd_y * d_error_y;
    
    /* 限幅 - X/Y 分离 */
    out_x = clamp(out_x, -OUTPUT_MAX_X, OUTPUT_MAX_X);
    out_y = clamp(out_y, -OUTPUT_MAX_Y, OUTPUT_MAX_Y);
    
    /* 最小移动阈值 */
    if (fabs_f(out_x) < MIN_MOVE_THRESHOLD) out_x = 0;
    if (fabs_f(out_y) < MIN_MOVE_THRESHOLD) out_y = 0;
    
    if (out_x == 0 && out_y == 0) return;
    
    /* 
     * 坐标映射 (需要反转方向):
     * - 目标在画面右边 (error_x > 0) -> 云台应向左转追踪 (delta_yaw < 0)
     * - 目标在画面下方 (error_y > 0) -> 云台应向下看追踪 (delta_pitch < 0)
     * 因此需要反转符号
     */
    float delta_yaw = -out_x;    /* 反转: 目标右 -> 云台左转 */
    float delta_pitch = -out_y;  /* 反转: 目标下 -> 云台下看 */
    
    /*
     * 渐进启动：限制首次移动的速度
     * 防止首次跟踪到目标时云台突然大幅运动
     */
    if (s_first_move) {
        if (delta_yaw > MAX_MOVE_SPEED_YAW) delta_yaw = MAX_MOVE_SPEED_YAW;
        else if (delta_yaw < -MAX_MOVE_SPEED_YAW) delta_yaw = -MAX_MOVE_SPEED_YAW;
        
        if (delta_pitch > MAX_MOVE_SPEED_PITCH) delta_pitch = MAX_MOVE_SPEED_PITCH;
        else if (delta_pitch < -MAX_MOVE_SPEED_PITCH) delta_pitch = -MAX_MOVE_SPEED_PITCH;
        
        /* 误差足够小后取消首次移动限制 */
        if (fabs_f(error_x) < s_deadzone * 2 && fabs_f(error_y) < s_deadzone * 2) {
            s_first_move = false;
        }
    }
    
    gimbal_move_delta(delta_yaw, delta_pitch, MOVE_TIME_MS);
}

void tracker_set_enable(bool enable)
{
    s_enabled = enable;
    if (!enable) {
        s_tracking = false;
        s_target_valid = false;
        s_filter_initialized = false;
        s_first_move = true;
        s_error_x_sum = 0;
        s_error_y_sum = 0;
    }
    printf("[Tracker] %s\r\n", enable ? "Enabled" : "Disabled");
}

bool tracker_is_tracking(void)
{
    return s_tracking;
}

void tracker_set_pid(float kp, float ki, float kd)
{
    /* 同时设置 X/Y，Y 轴用 0.75 倍系数 */
    s_kp_x = kp;
    s_ki_x = ki;
    s_kd_x = kd;
    s_kp_y = kp * 0.75f;
    s_ki_y = ki * 0.5f;
    s_kd_y = kd * 0.75f;
    printf("[Tracker] PID X: Kp=%.3f, Ki=%.3f, Kd=%.3f\r\n", s_kp_x, s_ki_x, s_kd_x);
    printf("[Tracker] PID Y: Kp=%.3f, Ki=%.3f, Kd=%.3f\r\n", s_kp_y, s_ki_y, s_kd_y);
}

void tracker_set_deadzone(int pixels)
{
    s_deadzone = pixels;
    printf("[Tracker] Deadzone: %d pixels\r\n", pixels);
}
