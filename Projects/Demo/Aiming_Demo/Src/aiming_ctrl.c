/**
 * @file    aiming_ctrl.c
 * @brief   视觉指向控制器实现 (直接位置映射)
 * 
 * @details 摄像头固定模式：云台直接指向摄像头看到的目标位置
 * 
 *          为什么不能用增量控制？
 *          - Tracking: 云台动 → 摄像头动 → 屏幕目标位置变 → 误差减小 (负反馈)
 *          - Aiming:   云台动 → 摄像头固定 → 屏幕目标不变 → 误差不变 (无反馈!)
 *          
 *          所以 Aiming 必须用直接位置映射：屏幕位置 → 云台绝对角度
 */

#include "aiming_ctrl.h"
#include "gimbal_ctrl.h"
#include <stdio.h>
#include <math.h>

/*===========================================================================
 * 配置参数
 *===========================================================================*/

/** 
 * @brief 运动时间 (ms) - 舵机到达目标位置的时间 
 * 注意：控制周期必须等于运动时间，否则舵机动作会被中断导致抖动
 */
#define MOVE_TIME_MS        30

/** @brief 控制周期 (ms) - 必须等于 MOVE_TIME_MS */
#define CONTROL_PERIOD_MS   MOVE_TIME_MS

/** @brief 目标丢失超时 (ms) */
#define TARGET_LOST_TIMEOUT_MS  500

/** @brief 默认死区 (像素) */
#define DEFAULT_DEADZONE        10

/** @brief 云台角度范围 */
#define YAW_RANGE           180.0f  /* -90 ~ +90 映射全屏宽度 */
#define PITCH_RANGE         90.0f   /* -90 ~ 0 映射全屏高度 */
#define PITCH_CENTER        (-45.0f)

/** @brief 最小移动阈值 (度) - 小于此值不发送指令 */
#define MIN_MOVE_THRESHOLD  0.8f

/** 
 * @brief 最大移动速度 (度/周期)
 * 限制每个周期的最大移动角度，实现渐进过渡
 * - 防止首次检测到目标时的突然跳跃
 * - 防止目标大范围移动时的突兀感
 */
#define MAX_MOVE_SPEED_YAW    5.0f   /* Yaw 每周期最大移动 */
#define MAX_MOVE_SPEED_PITCH  5.0f   /* Pitch 每周期最大移动 (更慢更稳) */

/** 
 * @brief 低通滤波系数 (0~1)
 * 
 * 滤波公式: filtered = alpha * new + (1-alpha) * old
 * - alpha 越小越平滑，但响应越慢
 * - 0.1 = 很平滑，适合静态目标
 * - 0.3 = 中等，适合缓慢移动
 * - 0.5 = 快速响应，但可能抖动
 */
#define FILTER_ALPHA_POSITION_X 0.3f    /* X 位置滤波 */
#define FILTER_ALPHA_POSITION_Y 0.15f   /* Y 位置滤波 (更强，减少上下抖动) */
#define FILTER_ALPHA_OUTPUT_YAW   0.35f /* Yaw 输出滤波 */
#define FILTER_ALPHA_OUTPUT_PITCH 0.2f  /* Pitch 输出滤波 (更强，减少上下抖动) */

/** @brief 调试打印周期 (ms) */
#define DEBUG_PRINT_PERIOD_MS   500

/*===========================================================================
 * 私有变量
 *===========================================================================*/

static uint32_t (*s_get_millis)(void) = NULL;
static int s_img_width = 320;
static int s_img_height = 240;

/* 调试 */
static uint32_t s_last_debug_time = 0;

/* 控制状态 */
static bool s_enabled = true;
static bool s_active = false;
static uint32_t s_last_target_time = 0;
static uint32_t s_last_control_time = 0;

/* 原始目标位置 */
static int s_target_cx = 0;
static int s_target_cy = 0;
static bool s_target_valid = false;

/* 滤波后的目标位置 */
static float s_filtered_cx = 0.0f;
static float s_filtered_cy = 0.0f;
static bool s_filter_initialized = false;

/* 上次输出的角度 */
static float s_last_yaw = 0.0f;
static float s_last_pitch = PITCH_CENTER;

/* 滤波后的输出角度 */
static float s_filtered_yaw = 0.0f;
static float s_filtered_pitch = PITCH_CENTER;

/* 抖动检测 */
static float s_prev_delta_yaw = 0.0f;
static float s_prev_delta_pitch = 0.0f;
static int s_yaw_reversal_count = 0;    /* Yaw 方向反转计数 */
static int s_pitch_reversal_count = 0;  /* Pitch 方向反转计数 */
static int s_sample_count = 0;          /* 采样计数 */

/* 死区 */
static int s_deadzone = DEFAULT_DEADZONE;

/*===========================================================================
 * 辅助函数
 *===========================================================================*/

static inline uint32_t millis(void)
{
    return s_get_millis ? s_get_millis() : 0;
}

static float fabs_f(float x)
{
    return (x >= 0) ? x : -x;
}

static float clamp_f(float val, float min_val, float max_val)
{
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

/*===========================================================================
 * API 实现
 *===========================================================================*/

void aiming_init(uint32_t (*get_millis)(void), int img_width, int img_height)
{
    s_get_millis = get_millis;
    s_img_width = img_width;
    s_img_height = img_height;
    
    s_enabled = true;
    s_active = false;
    s_target_valid = false;
    s_filter_initialized = false;
    
    s_last_yaw = 0.0f;
    s_last_pitch = PITCH_CENTER;
    s_filtered_yaw = 0.0f;
    s_filtered_pitch = PITCH_CENTER;
    
    printf("[Aiming] Init: image %dx%d\r\n", img_width, img_height);
    printf("[Aiming] Direct position mapping mode\r\n");
    printf("[Aiming] Filter: pos(%.2f,%.2f) out(%.2f,%.2f)\r\n", 
           FILTER_ALPHA_POSITION_X, FILTER_ALPHA_POSITION_Y,
           FILTER_ALPHA_OUTPUT_YAW, FILTER_ALPHA_OUTPUT_PITCH);
}

void aiming_update_target(const aiming_target_t *target)
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
    
    if (!s_active) {
        s_active = true;
        /* 首次获取目标，初始化位置滤波器 */
        s_filtered_cx = (float)s_target_cx;
        s_filtered_cy = (float)s_target_cy;
        s_filter_initialized = true;
        
        /* 计算目标角度（用于输出滤波器） */
        float norm_x = s_filtered_cx / (float)s_img_width;
        float norm_y = s_filtered_cy / (float)s_img_height;
        s_filtered_yaw = (0.5f - norm_x) * YAW_RANGE;
        s_filtered_pitch = -norm_y * PITCH_RANGE;
        
        /* 注意：保持 s_last_yaw/pitch 为当前云台位置（归中位置）
         * 这样 delta 会被正确计算，渐进过渡才能生效 */
        
        printf("[Aiming] Target acquired at (%d,%d) -> (%.1f, %.1f), gimbal at (%.1f, %.1f)\r\n", 
               s_target_cx, s_target_cy, s_filtered_yaw, s_filtered_pitch,
               s_last_yaw, s_last_pitch);
    }
}

void aiming_poll(void)
{
    if (!s_enabled) return;
    
    uint32_t now = millis();
    
    /* 检查目标超时 */
    if (s_target_valid && (now - s_last_target_time > TARGET_LOST_TIMEOUT_MS)) {
        s_target_valid = false;
        if (s_active) {
            s_active = false;
            s_filter_initialized = false;
            printf("[Aiming] Target lost\r\n");
        }
    }
    
    /* 控制周期检查 */
    if (now - s_last_control_time < CONTROL_PERIOD_MS) return;
    s_last_control_time = now;
    
    if (!s_target_valid || !s_active) return;
    
    /* 
     * 第一级滤波：平滑目标位置
     * 消除检测框的抖动，Y 方向使用更强的滤波
     */
    if (s_filter_initialized) {
        s_filtered_cx = FILTER_ALPHA_POSITION_X * (float)s_target_cx + 
                        (1.0f - FILTER_ALPHA_POSITION_X) * s_filtered_cx;
        s_filtered_cy = FILTER_ALPHA_POSITION_Y * (float)s_target_cy + 
                        (1.0f - FILTER_ALPHA_POSITION_Y) * s_filtered_cy;
    }
    
    /* 
     * 直接位置映射：屏幕位置 → 云台角度
     * 
     * 屏幕坐标:          云台角度:
     * X: 0 ~ width   →   Yaw: +90 ~ -90 (左边=云台右转指向左边)
     * Y: 0 ~ height  →   Pitch: 0 ~ -90 (上边=云台抬头)
     */
    
    /* 归一化到 0 ~ 1 */
    float norm_x = s_filtered_cx / (float)s_img_width;
    float norm_y = s_filtered_cy / (float)s_img_height;
    
    /* 映射到云台角度 */
    /* X: 屏幕左边(0) → 云台指向左边(+Yaw), 屏幕右边(1) → 云台指向右边(-Yaw) */
    float target_yaw = (0.5f - norm_x) * YAW_RANGE;
    
    /* Y: 屏幕上边(0) → 云台抬头(0), 屏幕下边(1) → 云台低头(-90) */
    float target_pitch = -norm_y * PITCH_RANGE;
    
    /* 限制范围 */
    target_yaw = clamp_f(target_yaw, GIMBAL_YAW_MIN, GIMBAL_YAW_MAX);
    target_pitch = clamp_f(target_pitch, GIMBAL_PITCH_MIN, GIMBAL_PITCH_MAX);
    
    /* 
     * 第二级滤波：平滑输出角度
     * 让云台运动更丝滑，Pitch 使用更强的滤波
     */
    s_filtered_yaw = FILTER_ALPHA_OUTPUT_YAW * target_yaw + 
                     (1.0f - FILTER_ALPHA_OUTPUT_YAW) * s_filtered_yaw;
    s_filtered_pitch = FILTER_ALPHA_OUTPUT_PITCH * target_pitch + 
                       (1.0f - FILTER_ALPHA_OUTPUT_PITCH) * s_filtered_pitch;
    
    /* 计算与上次输出的差值 */
    float delta_yaw = s_filtered_yaw - s_last_yaw;
    float delta_pitch = s_filtered_pitch - s_last_pitch;
    
    /* 抖动检测：检查方向反转 */
    s_sample_count++;
    /* 如果 delta 符号与上次相反，说明发生了方向反转 */
    if (s_prev_delta_yaw * delta_yaw < 0 && fabs_f(delta_yaw) > 0.1f) {
        s_yaw_reversal_count++;
    }
    if (s_prev_delta_pitch * delta_pitch < 0 && fabs_f(delta_pitch) > 0.1f) {
        s_pitch_reversal_count++;
    }
    s_prev_delta_yaw = delta_yaw;
    s_prev_delta_pitch = delta_pitch;
    
    /* 最小移动阈值 - 减少抖动 */
    if (fabs_f(delta_yaw) < MIN_MOVE_THRESHOLD && 
        fabs_f(delta_pitch) < MIN_MOVE_THRESHOLD) {
        return;
    }
    
    /* 
     * 渐进过渡：限制每周期最大移动角度
     * 大范围移动会自动分多步完成，避免突兀
     */
    float move_yaw = s_filtered_yaw;
    float move_pitch = s_filtered_pitch;
    
    if (delta_yaw > MAX_MOVE_SPEED_YAW) {
        move_yaw = s_last_yaw + MAX_MOVE_SPEED_YAW;
    } else if (delta_yaw < -MAX_MOVE_SPEED_YAW) {
        move_yaw = s_last_yaw - MAX_MOVE_SPEED_YAW;
    }
    
    if (delta_pitch > MAX_MOVE_SPEED_PITCH) {
        move_pitch = s_last_pitch + MAX_MOVE_SPEED_PITCH;
    } else if (delta_pitch < -MAX_MOVE_SPEED_PITCH) {
        move_pitch = s_last_pitch - MAX_MOVE_SPEED_PITCH;
    }
    
    /* 调试打印（限速后的实际输出） */
    if (now - s_last_debug_time >= DEBUG_PRINT_PERIOD_MS) {
        s_last_debug_time = now;
        /* 计算抖动率：反转次数/采样次数，越高越抖 */
        float jitter_yaw = (s_sample_count > 0) ? 
            (float)s_yaw_reversal_count / s_sample_count * 100.0f : 0;
        float jitter_pitch = (s_sample_count > 0) ? 
            (float)s_pitch_reversal_count / s_sample_count * 100.0f : 0;
        
        /* 打印: target=目标位置, move=实际输出 */
        printf("[Aim] tgt(%.0f,%.0f) move(%.0f,%.0f) jitter(%.0f%%,%.0f%%) [%c%c]\r\n",
               s_filtered_yaw, s_filtered_pitch,
               move_yaw, move_pitch,
               jitter_yaw, jitter_pitch,
               (jitter_yaw > 30) ? 'X' : (jitter_yaw > 15) ? '!' : 'o',
               (jitter_pitch > 30) ? 'X' : (jitter_pitch > 15) ? '!' : 'o');
        
        /* 重置计数器 */
        s_sample_count = 0;
        s_yaw_reversal_count = 0;
        s_pitch_reversal_count = 0;
    }
    
    /* 使用绝对位置控制 */
    gimbal_move(move_yaw, move_pitch, MOVE_TIME_MS);
    
    /* 记录本次实际输出（不是目标位置） */
    s_last_yaw = move_yaw;
    s_last_pitch = move_pitch;
}

void aiming_set_enable(bool enable)
{
    s_enabled = enable;
    if (!enable) {
        s_active = false;
        s_target_valid = false;
        s_filter_initialized = false;
    }
    printf("[Aiming] %s\r\n", enable ? "Enabled" : "Disabled");
}

bool aiming_is_active(void)
{
    return s_active;
}

void aiming_set_pid(float kp, float ki, float kd)
{
    /* 直接映射模式不使用 PID */
    (void)kp; (void)ki; (void)kd;
    printf("[Aiming] Direct mapping mode, PID not used\r\n");
}

void aiming_set_deadzone(int pixels)
{
    s_deadzone = pixels;
    printf("[Aiming] Deadzone: %d pixels\r\n", pixels);
}

void aiming_set_fov(float fov_x, float fov_y)
{
    /* 直接映射模式不使用 FOV */
    (void)fov_x; (void)fov_y;
    printf("[Aiming] Direct mapping mode, FOV not used\r\n");
}
