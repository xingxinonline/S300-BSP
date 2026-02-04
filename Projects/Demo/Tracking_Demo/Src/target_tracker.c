/**
 * @file    target_tracker.c
 * @brief   目标跟踪模块实现 (PID 控制 + track_id 跨帧关联)
 * 
 * 支持特性：
 *   - track_id 跨帧关联，防止目标切换抖动
 *   - 卡尔曼滤波速度用于预测补偿
 *   - X/Y 轴分离的 PID 参数
 */

#include "target_tracker.h"
#include "gimbal_ctrl.h"
#include <stdio.h>
#include <math.h>

/*===========================================================================
 * 配置参数
 *===========================================================================*/

/** 
 * @brief 控制周期 (ms) - 与运动时间匹配
 * 参考 Aiming_Demo：控制周期必须等于运动时间，否则舵机动作会被中断
 */
#define MOVE_TIME_MS            50      /* 舵机运动时间 */
#define CONTROL_PERIOD_MS       MOVE_TIME_MS  /* 控制周期 = 运动时间 */

/** @brief 目标丢失超时 (ms) */
#define TARGET_LOST_TIMEOUT_MS  500

/** @brief 默认死区 (像素) */
#define DEFAULT_DEADZONE        8       /* 稍微减小死区 */

/** 
 * @brief 自适应 PID 参数
 * 
 * 根据误差大小动态调整增益：
 * - 误差小（目标慢/接近中心）：低增益，更平滑
 * - 误差大（目标快/远离中心）：高增益，快速响应
 */
#define KP_X_MIN                0.045f  /* 小误差时的 Kp - 略微降低减少过冲 */
#define KP_X_MAX                0.10f   /* 大误差时的 Kp - 降低减少过冲 */
#define KP_Y_MIN                0.045f  /* Y 与 X 相同 */
#define KP_Y_MAX                0.10f   /* Y 与 X 相同 */

#define DEFAULT_KI_X            0.002f  /* 积分系数 - 降低减少累积 */
#define DEFAULT_KD_X            0.045f  /* 微分系数 - 提高增加阻尼 */
#define DEFAULT_KI_Y            0.002f  /* Y 与 X 相同 */
#define DEFAULT_KD_Y            0.045f  /* Y 与 X 相同 */

/** 
 * @brief 自适应增益的误差阈值 (像素)
 * - 误差 < ERR_LOW：使用 KP_MIN
 * - 误差 > ERR_HIGH：使用 KP_MAX
 * - 中间：线性插值
 */
#define ADAPTIVE_ERR_LOW        10.0f   /* 降低阈值，更早进入高增益 */
#define ADAPTIVE_ERR_HIGH       40.0f   /* 降低阈值 */

/** @brief 积分限幅 */
#define INTEGRAL_MAX            30.0f   /* 降低积分限幅减少过冲 */

/** @brief 输出限幅 (度/次) */
#define OUTPUT_MAX_X            8.0f    /* 大幅提高限幅 */
#define OUTPUT_MAX_Y            8.0f    /* Pitch 与 Yaw 相同 */

/** @brief 最小运动阈值 (度) */
#define MIN_MOVE_THRESHOLD      0.15f   /* 进一步减小阈值 */

/**
 * @brief 目标位置低通滤波系数 (0~1)
 * 系数越高跟随越快，但护动可能更大
 */
#define FILTER_ALPHA_X          0.8f    /* 略微降低，更平滑 */
#define FILTER_ALPHA_Y          0.8f    /* Y 与 X 相同 */

/**
 * @brief 输出低通滤波系数
 * 系数越高跟随越快
 */
#define OUTPUT_FILTER_ALPHA_YAW   0.85f  /* 略微降低，更平滑 */
#define OUTPUT_FILTER_ALPHA_PITCH 0.85f  /* Pitch 与 Yaw 相同 */

/**
 * @brief 渐进启动：最大输出速度 (度/周期)
 * 提高最大速度
 */
#define MAX_MOVE_SPEED_YAW      10.0f   /* 大幅提高最大速度 */
#define MAX_MOVE_SPEED_PITCH    10.0f   /* Pitch 与 Yaw 相同 */

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

/* X/Y 分离的 PID 参数 (Ki, Kd 固定，Kp 自适应) */
static float s_ki_x = DEFAULT_KI_X;
static float s_kd_x = DEFAULT_KD_X;
static float s_ki_y = DEFAULT_KI_Y;
static float s_kd_y = DEFAULT_KD_Y;

/* PID 状态 */
static float s_error_x_sum = 0.0f;
static float s_error_y_sum = 0.0f;
static float s_error_x_prev = 0.0f;
static float s_error_y_prev = 0.0f;

/* 上次输出 (用于渐进启动和输出滤波) */
static float s_last_delta_yaw = 0.0f;
static float s_last_delta_pitch = 0.0f;
static bool s_first_move = true;

/* 输出滤波 */
static float s_filtered_out_yaw = 0.0f;
static float s_filtered_out_pitch = 0.0f;

/* 抖动检测 (参考 Aiming_Demo) */
static float s_prev_delta_yaw = 0.0f;
static float s_prev_delta_pitch = 0.0f;
static int s_yaw_reversal_count = 0;
static int s_pitch_reversal_count = 0;
static int s_sample_count = 0;

/* 死区 */
static int s_deadzone = DEFAULT_DEADZONE;

/* track_id 跨帧关联 */
static uint8_t s_current_track_id = 0;       /**< 当前跟踪的 track_id */
static bool s_has_track_id = false;          /**< 是否有有效的 track_id */
static uint32_t s_track_id_lost_time = 0;    /**< track_id 丢失时间 */

/** @brief track_id 丢失后保持跟踪的宽限期 (ms) */
#define TRACK_ID_GRACE_PERIOD_MS  300

/* 卡尔曼滤波速度 */
static int8_t s_target_vx = 0;               /**< 目标 X 速度 */
static int8_t s_target_vy = 0;               /**< 目标 Y 速度 */
static uint8_t s_kf_confidence = 0;          /**< 卡尔曼置信度 */

/* 云台运动补偿 */
static float s_last_gimbal_delta_yaw = 0.0f;   /**< 上一次云台 Yaw 增量 (度) */
static float s_last_gimbal_delta_pitch = 0.0f; /**< 上一次云台 Pitch 增量 (度) */
static float s_compensated_vx = 0.0f;          /**< 补偿后的目标 X 速度 */
static float s_compensated_vy = 0.0f;          /**< 补偿后的目标 Y 速度 */

/**
 * @brief 云台运动补偿配置
 * 
 * DSP 的卡尔曼滤波器观测到的速度 = 目标真实速度 + 相机运动引起的位移
 * CM4 需要补偿掉相机运动，才能得到目标的真实速度
 */
#define CAMERA_FOV_X_DEG        78.0f   /**< 摄像头水平视场角 (度) */
#define CAMERA_FOV_Y_DEG        60.0f   /**< 摄像头垂直视场角 (度) */

/**
 * @brief 速度预测配置
 * 
 * - VELOCITY_PREDICT_FACTOR: 速度预测的权重 (0~1)
 *   0.0 = 完全禁用, 1.0 = 完全使用补偿后的速度
 * - MIN_KF_CONFIDENCE: 使用速度预测的最小卡尔曼置信度
 */
#define VELOCITY_PREDICT_FACTOR  0.5f    /**< 速度预测权重 (启用) */
#define MIN_KF_CONFIDENCE        70      /**< 最小置信度 (0~100) */

/*===========================================================================
 * 调试统计
 *===========================================================================*/

/** @brief 调试打印周期 (ms) */
#define DEBUG_PRINT_PERIOD_MS   1000

static uint32_t s_debug_last_print_time = 0;
static uint32_t s_debug_poll_count = 0;      /**< poll 调用计数 */
static uint32_t s_debug_control_count = 0;   /**< 实际控制计数 */
static uint32_t s_debug_deadzone_count = 0;  /**< 死区跳过计数 */
static float s_debug_last_error_x = 0;       /**< 最后一次误差 X */
static float s_debug_last_error_y = 0;       /**< 最后一次误差 Y */
static float s_debug_last_delta_yaw = 0;     /**< 最后一次云台增量 Yaw */
static float s_debug_last_delta_pitch = 0;   /**< 最后一次云台增量 Pitch */

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

/**
 * @brief 计算自适应 Kp
 * 
 * 根据误差大小线性插值：
 * - 误差 <= ERR_LOW：返回 kp_min
 * - 误差 >= ERR_HIGH：返回 kp_max
 * - 中间：线性插值
 */
static float adaptive_kp(float error, float kp_min, float kp_max)
{
    float abs_err = fabs_f(error);
    
    if (abs_err <= ADAPTIVE_ERR_LOW) {
        return kp_min;
    } else if (abs_err >= ADAPTIVE_ERR_HIGH) {
        return kp_max;
    } else {
        /* 线性插值 */
        float ratio = (abs_err - ADAPTIVE_ERR_LOW) / (ADAPTIVE_ERR_HIGH - ADAPTIVE_ERR_LOW);
        return kp_min + ratio * (kp_max - kp_min);
    }
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
    
    /* 重置输出滤波 */
    s_filtered_out_yaw = 0;
    s_filtered_out_pitch = 0;
    
    /* 重置抖动检测 */
    s_prev_delta_yaw = 0;
    s_prev_delta_pitch = 0;
    s_yaw_reversal_count = 0;
    s_pitch_reversal_count = 0;
    s_sample_count = 0;
    
    /* 重置 track_id 跟踪 */
    s_current_track_id = 0;
    s_has_track_id = false;
    s_track_id_lost_time = 0;
    
    /* 重置卡尔曼速度 */
    s_target_vx = 0;
    s_target_vy = 0;
    s_kf_confidence = 0;
    
    /* 重置云台运动补偿 */
    s_last_gimbal_delta_yaw = 0;
    s_last_gimbal_delta_pitch = 0;
    s_compensated_vx = 0;
    s_compensated_vy = 0;
    
    printf("[Tracker] Init: image %dx%d, center (%d,%d)\r\n",
           img_width, img_height, s_img_cx, s_img_cy);
    printf("[Tracker] Camera FOV: %.0fx%.0f deg, velocity predict=%.1f, min_conf=%d\r\n",
           CAMERA_FOV_X_DEG, CAMERA_FOV_Y_DEG, VELOCITY_PREDICT_FACTOR, MIN_KF_CONFIDENCE);
    printf("[Tracker] Adaptive PID X: Kp=%.3f~%.3f, Ki=%.3f, Kd=%.3f\r\n", 
           KP_X_MIN, KP_X_MAX, s_ki_x, s_kd_x);
    printf("[Tracker] Adaptive PID Y: Kp=%.3f~%.3f, Ki=%.3f, Kd=%.3f\r\n", 
           KP_Y_MIN, KP_Y_MAX, s_ki_y, s_kd_y);
    printf("[Tracker] Adaptive range: err %.0f~%.0f px\r\n", 
           ADAPTIVE_ERR_LOW, ADAPTIVE_ERR_HIGH);
    printf("[Tracker] Filter: X=%.2f, Y=%.2f\r\n", FILTER_ALPHA_X, FILTER_ALPHA_Y);
    printf("[Tracker] track_id association enabled\r\n");
}

void tracker_update_target(const tracker_target_t *target)
{
    if (!target || !target->valid) {
        s_target_valid = false;
        return;
    }
    
    uint32_t now = millis();
    
    /*
     * track_id 跨帧关联逻辑：
     * 1. 如果没有跟踪的 track_id，接受新目标
     * 2. 如果有跟踪的 track_id，只接受相同 track_id 的目标
     * 3. 如果当前 track_id 丢失超过宽限期，接受新目标
     */
    if (s_has_track_id && target->track_id != 0) {
        if (target->track_id != s_current_track_id) {
            /* track_id 不匹配，检查是否超过宽限期 */
            if (s_track_id_lost_time == 0) {
                /* 开始计时 */
                s_track_id_lost_time = now;
            } else if (now - s_track_id_lost_time > TRACK_ID_GRACE_PERIOD_MS) {
                /* 超过宽限期，接受新目标 */
                printf("[Tracker] track_id changed: %u -> %u\r\n", 
                       s_current_track_id, target->track_id);
                s_current_track_id = target->track_id;
                s_track_id_lost_time = 0;
                s_first_move = true;  /* 重置首次移动 */
            } else {
                /* 在宽限期内，忽略不匹配的目标 */
                return;
            }
        } else {
            /* track_id 匹配，重置丢失计时 */
            s_track_id_lost_time = 0;
        }
    } else if (target->track_id != 0) {
        /* 没有跟踪的 track_id，接受新目标 */
        s_current_track_id = target->track_id;
        s_has_track_id = true;
        printf("[Tracker] New track_id: %u\r\n", target->track_id);
    }
    
    /* 计算目标中心 */
    s_target_cx = (target->x1 + target->x2) / 2;
    s_target_cy = (target->y1 + target->y2) / 2;
    s_target_valid = true;
    s_last_target_time = now;
    
    /* 保存卡尔曼速度 */
    s_target_vx = target->vx;
    s_target_vy = target->vy;
    s_kf_confidence = target->kf_confidence;
    
    if (!s_tracking) {
        s_tracking = true;
        s_first_move = true;  /* 标记首次移动 */
        
        /* 初始化滤波器 */
        s_filtered_cx = (float)s_target_cx;
        s_filtered_cy = (float)s_target_cy;
        s_filter_initialized = true;
        
        printf("[Tracker] Target acquired at (%d,%d) track_id=%u\r\n", 
               s_target_cx, s_target_cy, s_current_track_id);
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
            s_has_track_id = false;
            s_current_track_id = 0;
            s_track_id_lost_time = 0;
            printf("[Tracker] Target lost (track_id=%u)\r\n", s_current_track_id);
            /* 重置 PID 积分 */
            s_error_x_sum = 0;
            s_error_y_sum = 0;
        }
    }
    
    /* 控制周期检查 */
    if (now - s_last_control_time < CONTROL_PERIOD_MS) return;
    s_last_control_time = now;
    
    /* 统计 poll 次数（过了周期检查的） */
    s_debug_poll_count++;
    
    if (!s_target_valid || !s_tracking) return;
    
    /* 
     * 第一级滤波：平滑目标位置
     * 消除检测框的抖动
     */
    if (s_filter_initialized) {
        s_filtered_cx = FILTER_ALPHA_X * (float)s_target_cx + 
                        (1.0f - FILTER_ALPHA_X) * s_filtered_cx;
        s_filtered_cy = FILTER_ALPHA_Y * (float)s_target_cy + 
                        (1.0f - FILTER_ALPHA_Y) * s_filtered_cy;
    }
    
    /* 计算误差 (滤波位置相对图像中心的偏移) */
    float error_x = s_filtered_cx - (float)s_img_cx;
    float error_y = s_filtered_cy - (float)s_img_cy;
    
    /*
     * 云台运动补偿：计算目标的真实速度
     *
     * 当云台向右转 (delta_yaw > 0)：
     *   - 画面中物体向左移动 → DSP 观测到负的假速度
     *   - 假速度 = -(delta_yaw / FOV) * width（负值）
     *   - 真实速度 = 观测速度 - 假速度 = 观测速度 + gimbal_px
     *
     * 当云台向左转 (delta_yaw < 0)：
     *   - 画面中物体向右移动 → DSP 观测到正的假速度
     *   - 假速度 = -(delta_yaw / FOV) * width（正值）
     *   - 真实速度 = 观测速度 - 假速度 = 观测速度 + gimbal_px
     *
     * 统一公式：真实速度 = 观测速度 + (delta / FOV) * size
     */
    float gimbal_px_x = (s_last_gimbal_delta_yaw / CAMERA_FOV_X_DEG) * s_img_width;
    float gimbal_px_y = (s_last_gimbal_delta_pitch / CAMERA_FOV_Y_DEG) * s_img_height;
    
    /* 补偿后的真实速度（加法，因为要抵消相机运动的影响） */
    s_compensated_vx = (float)s_target_vx + gimbal_px_x;
    s_compensated_vy = (float)s_target_vy + gimbal_px_y;
    
    /*
     * 速度预测补偿：使用补偿后的速度预测目标位置
     * 只有在卡尔曼置信度足够高时才启用
     */
    if (VELOCITY_PREDICT_FACTOR > 0 && s_kf_confidence >= MIN_KF_CONFIDENCE) {
        error_x += VELOCITY_PREDICT_FACTOR * s_compensated_vx;
        error_y += VELOCITY_PREDICT_FACTOR * s_compensated_vy;
    }
    
    /* 保存原始误差用于调试 */
    s_debug_last_error_x = error_x;
    s_debug_last_error_y = error_y;
    
    /* 死区处理 */
    if (fabs_f(error_x) < s_deadzone) error_x = 0;
    if (fabs_f(error_y) < s_deadzone) error_y = 0;
    
    if (error_x == 0 && error_y == 0) {
        s_debug_deadzone_count++;
        return;
    }
    
    /* 实际执行控制 */
    s_debug_control_count++;
    
    /* 自适应 PID 计算 - X/Y 轴分离参数 */
    
    /* 计算自适应 Kp：误差大时增益高，误差小时增益低 */
    float kp_x = adaptive_kp(error_x, KP_X_MIN, KP_X_MAX);
    float kp_y = adaptive_kp(error_y, KP_Y_MIN, KP_Y_MAX);
    
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
    
    /* 输出 - 使用自适应 Kp */
    float out_x = kp_x * error_x + s_ki_x * s_error_x_sum + s_kd_x * d_error_x;
    float out_y = kp_y * error_y + s_ki_y * s_error_y_sum + s_kd_y * d_error_y;
    
    /* 限幅 - X/Y 分离 */
    out_x = clamp(out_x, -OUTPUT_MAX_X, OUTPUT_MAX_X);
    out_y = clamp(out_y, -OUTPUT_MAX_Y, OUTPUT_MAX_Y);
    
    /* 
     * 坐标映射 (需要反转方向):
     * - 目标在画面右边 (error_x > 0) -> 云台应向左转追踪 (delta_yaw < 0)
     * - 目标在画面下方 (error_y > 0) -> 云台应向下看追踪 (delta_pitch < 0)
     */
    float delta_yaw = -out_x;
    float delta_pitch = -out_y;
    
    /*
     * 第二级滤波：平滑 PID 输出
     * 参考 Aiming_Demo，让云台运动更丝滑
     */
    s_filtered_out_yaw = OUTPUT_FILTER_ALPHA_YAW * delta_yaw + 
                         (1.0f - OUTPUT_FILTER_ALPHA_YAW) * s_filtered_out_yaw;
    s_filtered_out_pitch = OUTPUT_FILTER_ALPHA_PITCH * delta_pitch + 
                           (1.0f - OUTPUT_FILTER_ALPHA_PITCH) * s_filtered_out_pitch;
    
    delta_yaw = s_filtered_out_yaw;
    delta_pitch = s_filtered_out_pitch;
    
    /* 最小移动阈值 */
    if (fabs_f(delta_yaw) < MIN_MOVE_THRESHOLD && 
        fabs_f(delta_pitch) < MIN_MOVE_THRESHOLD) {
        return;
    }
    
    /*
     * 渐进启动：限制移动速度
     */
    if (delta_yaw > MAX_MOVE_SPEED_YAW) delta_yaw = MAX_MOVE_SPEED_YAW;
    else if (delta_yaw < -MAX_MOVE_SPEED_YAW) delta_yaw = -MAX_MOVE_SPEED_YAW;
    
    if (delta_pitch > MAX_MOVE_SPEED_PITCH) delta_pitch = MAX_MOVE_SPEED_PITCH;
    else if (delta_pitch < -MAX_MOVE_SPEED_PITCH) delta_pitch = -MAX_MOVE_SPEED_PITCH;
    
    /* 抖动检测：检查方向反转 (参考 Aiming_Demo) */
    s_sample_count++;
    if (s_prev_delta_yaw * delta_yaw < 0 && fabs_f(delta_yaw) > 0.1f) {
        s_yaw_reversal_count++;
    }
    if (s_prev_delta_pitch * delta_pitch < 0 && fabs_f(delta_pitch) > 0.1f) {
        s_pitch_reversal_count++;
    }
    s_prev_delta_yaw = delta_yaw;
    s_prev_delta_pitch = delta_pitch;
    
    /* 保存调试数据 */
    s_debug_last_delta_yaw = delta_yaw;
    s_debug_last_delta_pitch = delta_pitch;
    
    /* 周期性打印调试信息 */
    if (now - s_debug_last_print_time >= DEBUG_PRINT_PERIOD_MS) {
        s_debug_last_print_time = now;
        
        /* 计算抖动率 */
        float jitter_yaw = (s_sample_count > 0) ? 
            (float)s_yaw_reversal_count / s_sample_count * 100.0f : 0;
        float jitter_pitch = (s_sample_count > 0) ? 
            (float)s_pitch_reversal_count / s_sample_count * 100.0f : 0;
        
        printf("[Tracker] id=%u pos=(%d,%d) err=(%.0f,%.0f) out=(%.1f,%.1f)\r\n",
               s_current_track_id,
               s_target_cx, s_target_cy,
               s_debug_last_error_x, s_debug_last_error_y,
               s_debug_last_delta_yaw, s_debug_last_delta_pitch);
        
        /* 如果启用了速度预测，打印补偿信息 */
        if (VELOCITY_PREDICT_FACTOR > 0) {
            printf("[Tracker] vel: raw=(%d,%d) comp=(%.1f,%.1f) conf=%u jitter=(%.0f%%,%.0f%%)\r\n",
                   s_target_vx, s_target_vy,
                   s_compensated_vx, s_compensated_vy,
                   s_kf_confidence,
                   jitter_yaw, jitter_pitch);
        }
        
        /* 重置计数器 */
        s_sample_count = 0;
        s_yaw_reversal_count = 0;
        s_pitch_reversal_count = 0;
        s_debug_poll_count = 0;
        s_debug_control_count = 0;
        s_debug_deadzone_count = 0;
    }
    
    /* 记录本次云台运动量，用于下一帧的速度补偿 */
    s_last_gimbal_delta_yaw = delta_yaw;
    s_last_gimbal_delta_pitch = delta_pitch;
    
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
        s_has_track_id = false;
        s_current_track_id = 0;
        s_track_id_lost_time = 0;
    }
    printf("[Tracker] %s\r\n", enable ? "Enabled" : "Disabled");
}

bool tracker_is_tracking(void)
{
    return s_tracking;
}

void tracker_set_pid(float kp, float ki, float kd)
{
    /* 自适应模式：只调整 Ki, Kd，Kp 由自适应算法控制 */
    s_ki_x = ki;
    s_kd_x = kd;
    s_ki_y = ki * 0.5f;
    s_kd_y = kd * 0.75f;
    printf("[Tracker] Set Ki=%.3f, Kd=%.3f (Kp is adaptive)\r\n", ki, kd);
}

void tracker_set_deadzone(int pixels)
{
    s_deadzone = pixels;
    printf("[Tracker] Deadzone: %d pixels\r\n", pixels);
}

uint8_t tracker_get_current_track_id(void)
{
    return s_has_track_id ? s_current_track_id : 0;
}

void tracker_reset(void)
{
    s_tracking = false;
    s_target_valid = false;
    s_filter_initialized = false;
    s_first_move = true;
    s_error_x_sum = 0;
    s_error_y_sum = 0;
    s_has_track_id = false;
    s_current_track_id = 0;
    s_track_id_lost_time = 0;
    s_target_vx = 0;
    s_target_vy = 0;
    s_kf_confidence = 0;
    s_last_gimbal_delta_yaw = 0;
    s_last_gimbal_delta_pitch = 0;
    s_compensated_vx = 0;
    s_compensated_vy = 0;
    s_filtered_out_yaw = 0;
    s_filtered_out_pitch = 0;
    s_prev_delta_yaw = 0;
    s_prev_delta_pitch = 0;
    s_yaw_reversal_count = 0;
    s_pitch_reversal_count = 0;
    s_sample_count = 0;
    printf("[Tracker] Reset\r\n");
}
