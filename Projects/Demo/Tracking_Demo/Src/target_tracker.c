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
#define DEFAULT_DEADZONE        15      /* 增大死区减少小误差振荡 */

/** 
 * @brief 自适应 PID 参数
 * 
 * 根据误差大小动态调整增益：
 * - 误差小（目标慢/接近中心）：低增益，更平滑
 * - 误差大（目标快/远离中心）：高增益，快速响应
 */
#define KP_X_MIN                0.035f  /* 小误差时的 Kp - 降低减少过冲 */
#define KP_X_MAX                0.080f  /* 大误差时的 Kp - 降低减少过冲 */
#define KP_Y_MIN                0.035f  /* Y 与 X 相同 */
#define KP_Y_MAX                0.080f  /* Y 与 X 相同 */

#define DEFAULT_KI_X            0.001f  /* 积分系数 - 降低减少累积 */
#define DEFAULT_KD_X            0.060f  /* 微分系数 - 提高增加阻尼 */
#define DEFAULT_KI_Y            0.001f  /* Y 与 X 相同 */
#define DEFAULT_KD_Y            0.060f  /* Y 与 X 相同 */

/** 
 * @brief 自适应增益的误差阈值 (像素)
 * - 误差 < ERR_LOW：使用 KP_MIN
 * - 误差 > ERR_HIGH：使用 KP_MAX
 * - 中间：线性插值
 */
#define ADAPTIVE_ERR_LOW        10.0f   /* 降低阈值，更早进入高增益 */
#define ADAPTIVE_ERR_HIGH       40.0f   /* 降低阈值 */

/**
 * @brief 基于目标框大小的自适应增益
 * 
 * 目标近（框大）：增益低，避免过冲抖动
 * 目标远（框小）：增益高，保证响应速度
 * 
 * 框大小用面积占比衡量：box_area / image_area
 * 实测范围约 3%-43%，调整阈值以获得更好的渐变效果
 */
#define BOX_SIZE_RATIO_LARGE    0.25f   /* 框占比 >25% 认为近距离（大框）*/
#define BOX_SIZE_RATIO_SMALL    0.05f   /* 框占比 <5% 认为远距离（小框）*/
#define BOX_SIZE_GAIN_NEAR      0.5f    /* 近距离增益系数（降低增益）*/
#define BOX_SIZE_GAIN_FAR       1.15f   /* 远距离增益系数（适度提高，避免过冲）*/

/** @brief 积分限幅 */
#define INTEGRAL_MAX            20.0f   /* 降低积分限幅减少过冲 */

/** @brief 输出限幅 (度/次) */
#define OUTPUT_MAX_X            5.0f    /* 限幅 */
#define OUTPUT_MAX_Y            5.0f    /* Pitch 与 Yaw 相同 */

/** @brief 最小运动阈值 (度) */
#define MIN_MOVE_THRESHOLD      0.25f   /* 略微提高 */

/**
 * @brief 目标位置低通滤波系数 (0~1)
 * 系数越低越平滑，但响应越慢
 */
#define FILTER_ALPHA_X          0.70f   /* 平衡滤波和响应 */
#define FILTER_ALPHA_Y          0.70f   /* Y 与 X 相同 */

/**
 * @brief 输出低通滤波系数
 * 系数越低越平滑
 */
#define OUTPUT_FILTER_ALPHA_YAW   0.65f  /* 平衡滤波和响应 */
#define OUTPUT_FILTER_ALPHA_PITCH 0.65f  /* Pitch 与 Yaw 相同 */

/**
 * @brief 渐进启动：最大输出速度 (度/周期)
 */
#define MAX_MOVE_SPEED_YAW      3.5f    /* 最大速度（度/周期），限制云台追踪速度 */
#define MAX_MOVE_SPEED_PITCH    3.5f    /* Pitch 与 Yaw 相同 */

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

/* 目标框大小（用于自适应增益）*/
static float s_box_size_ratio = 0.05f;   /**< 框面积占图像面积比例 */
static float s_box_size_gain = 1.0f;     /**< 基于框大小的增益系数 */

/* track_id 跨帧关联 */
static uint8_t s_current_track_id = 0;       /**< 当前跟踪的 track_id */

/*===========================================================================
 * M4 状态机 (按 DSP_Tracker_Design_v2.1 设计)
 * 
 * IDLE → TRACKING → PREDICTING → LOST
 *                       ↑
 *       目标消失时用 vx/vy 预测位置
 * 
 * 与 DSP TrackSlot 漏检容忍配合：
 * - DSP: 漏检 15 帧 (~500ms) 内保留 track_id 槽位
 * - M4:  预测 10 帧 (~500ms) 后判定丢失
 * - 如果目标在 DSP 容忍期内重现，M4 可从 PREDICTING 恢复到 TRACKING
 *===========================================================================*/
typedef enum {
    TRACKER_STATE_IDLE,         /**< 空闲：无跟踪目标 */
    TRACKER_STATE_TRACKING,     /**< 跟踪：正常跟踪目标 */
    TRACKER_STATE_PREDICTING,   /**< 预测：目标消失，用速度预测位置 */
    TRACKER_STATE_LOST          /**< 丢失：预测超时，等待重选 */
} TrackerState_t;

static TrackerState_t s_state = TRACKER_STATE_IDLE;
static uint32_t s_state_enter_time = 0;      /**< 进入当前状态的时间 */

/* 遮挡预测相关 */
static int s_predict_count = 0;              /**< 预测帧计数 */
static int s_predict_cx = 0;                 /**< 预测位置 X */
static int s_predict_cy = 0;                 /**< 预测位置 Y */
static int8_t s_last_vx = 0;                 /**< 上一帧速度 X */
static int8_t s_last_vy = 0;                 /**< 上一帧速度 Y */
static uint8_t s_lost_track_id = 0;          /**< 丢失时的 track_id，用于优先恢复 */
static uint8_t s_lost_edge_flags = 0;        /**< 丢失时的边缘标记 */
static uint8_t s_last_edge_flags = 0;        /**< 最后一帧的边缘标记 */
static int s_lost_cx = 0;                    /**< 丢失时的目标位置 X */
static int s_lost_cy = 0;                    /**< 丢失时的目标位置 Y */

/** 
 * @brief 最大预测帧数（超过则判定丢失）
 * @note  减少预测帧数，避免云台运动过多导致目标丢失
 *        5 帧 × 50ms = 250ms
 */
#define MAX_PREDICT_FRAMES        5
/** @brief 丢失后的冷却时间 (ms) */
#define LOST_COOLDOWN_SAME_ID_MS  50    /* 原 ID 回来只需短冷却 */
#define LOST_COOLDOWN_NEW_ID_MS   500   /* 新 ID 需要长冷却，避免误切换 */
/** @brief LOST 状态超时时间 (ms) */
#define LOST_REACQUIRE_MS         1000  /* 1秒后可重新寻找任意目标 */
#define LOST_RETURN_HOME_MS       10000 /* 10秒后恢复到初始位置 */
/** @brief 同 ID 恢复时的最大位置偏差 (像素) */
#define SAME_ID_MAX_POSITION_DIFF 40    /* 超过此值认为是误分配，不是同一个人 */
/** @brief 遮挡预测时速度上限 (像素/帧)，防止预测位置飞出画面 */
#define MAX_PREDICT_VELOCITY      3
/** @brief 位置跳变阈值 (像素)，超过此值需要结合速度判断 */
#define POSITION_JUMP_THRESHOLD   50

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
 * 
 * 注意：实测发现速度预测会导致过冲（云台比目标转得快），
 *       因此暂时禁用。如需启用，需要配合更低的 PID 增益。
 */
#define VELOCITY_PREDICT_FACTOR  0.0f    /**< 禁用速度预测，避免过冲 */
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
    
    /* 重置状态机 */
    s_state = TRACKER_STATE_IDLE;
    s_state_enter_time = 0;
    s_predict_count = 0;
    s_predict_cx = 0;
    s_predict_cy = 0;
    s_last_vx = 0;
    s_last_vy = 0;
    
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
    printf("[Tracker] State machine: IDLE->TRACKING->PREDICTING->LOST\r\n");
    printf("[Tracker] Max predict frames: %d, lost cooldown: same=%dms, new=%dms\r\n",
           MAX_PREDICT_FRAMES, LOST_COOLDOWN_SAME_ID_MS, LOST_COOLDOWN_NEW_ID_MS);
}

/**
 * @brief 计算两点距离的平方
 */
static inline int distance_sq(int x1, int y1, int x2, int y2)
{
    int dx = x1 - x2;
    int dy = y1 - y2;
    return dx * dx + dy * dy;
}

/**
 * @brief 状态名称（用于调试）
 */
static const char* state_name(TrackerState_t state)
{
    switch (state) {
        case TRACKER_STATE_IDLE:       return "IDLE";
        case TRACKER_STATE_TRACKING:   return "TRACKING";
        case TRACKER_STATE_PREDICTING: return "PREDICTING";
        case TRACKER_STATE_LOST:       return "LOST";
        default:                       return "UNKNOWN";
    }
}

/**
 * @brief 切换状态
 */
static void change_state(TrackerState_t new_state)
{
    if (s_state != new_state) {
        printf("[Tracker] State: %s -> %s\r\n", state_name(s_state), state_name(new_state));
        s_state = new_state;
        s_state_enter_time = millis();
    }
}

void tracker_update_target(const tracker_target_t *target)
{
    uint32_t now = millis();
    
    /*
     * v2 设计：简化逻辑
     * - main.c 已经做了目标确认（BOX_CONFIRM）
     * - 这里只接收已确认的目标，直接进行跟踪
     * - 重点是处理遮挡预测
     */
    
    /* 处理无效目标输入 */
    if (!target || !target->valid) {
        /* 目标消失，检查当前状态 */
        if (s_state == TRACKER_STATE_TRACKING) {
            /* 正在跟踪中，进入预测模式 */
            change_state(TRACKER_STATE_PREDICTING);
            s_predict_count = 0;
            s_predict_cx = s_target_cx;
            s_predict_cy = s_target_cy;
            /* 保存最后的速度用于预测，限制上限防止预测飞出画面 */
            s_last_vx = s_target_vx;
            s_last_vy = s_target_vy;
            if (s_last_vx > MAX_PREDICT_VELOCITY) s_last_vx = MAX_PREDICT_VELOCITY;
            if (s_last_vx < -MAX_PREDICT_VELOCITY) s_last_vx = -MAX_PREDICT_VELOCITY;
            if (s_last_vy > MAX_PREDICT_VELOCITY) s_last_vy = MAX_PREDICT_VELOCITY;
            if (s_last_vy < -MAX_PREDICT_VELOCITY) s_last_vy = -MAX_PREDICT_VELOCITY;
        }
        s_target_valid = false;
        return;
    }
    
    int target_cx = (target->x1 + target->x2) / 2;
    int target_cy = (target->y1 + target->y2) / 2;
    
    /*
     * 计算目标框大小占比，用于自适应增益
     * 近距离（大框）降低增益，远距离（小框）提高增益
     */
    {
        int box_w = target->x2 - target->x1;
        int box_h = target->y2 - target->y1;
        if (box_w < 0) box_w = -box_w;
        if (box_h < 0) box_h = -box_h;
        int box_area = box_w * box_h;
        int img_area = s_img_width * s_img_height;
        
        s_box_size_ratio = (float)box_area / (float)img_area;
        
        /* 根据框大小计算增益系数 */
        if (s_box_size_ratio >= BOX_SIZE_RATIO_LARGE) {
            /* 大框（近距离）：使用低增益 */
            s_box_size_gain = BOX_SIZE_GAIN_NEAR;
        } else if (s_box_size_ratio <= BOX_SIZE_RATIO_SMALL) {
            /* 小框（远距离）：使用高增益 */
            s_box_size_gain = BOX_SIZE_GAIN_FAR;
        } else {
            /* 中间：线性插值 */
            float t = (s_box_size_ratio - BOX_SIZE_RATIO_SMALL) / 
                      (BOX_SIZE_RATIO_LARGE - BOX_SIZE_RATIO_SMALL);
            s_box_size_gain = BOX_SIZE_GAIN_FAR + t * (BOX_SIZE_GAIN_NEAR - BOX_SIZE_GAIN_FAR);
        }
    }
    
    /*
     * 状态机处理
     */
    switch (s_state) {
    case TRACKER_STATE_IDLE:
        /* 空闲状态：接受任何目标，立即锁定 */
        s_current_track_id = target->track_id;
        s_target_cx = target_cx;
        s_target_cy = target_cy;
        s_target_valid = true;
        s_last_target_time = now;
        
        /* 保存卡尔曼速度和边缘标记 */
        s_target_vx = target->vx;
        s_target_vy = target->vy;
        s_kf_confidence = target->kf_confidence;
        s_last_edge_flags = target->edge_flags;
        
        /* 初始化跟踪 */
        s_tracking = true;
        s_first_move = true;
        s_filtered_cx = (float)target_cx;
        s_filtered_cy = (float)target_cy;
        s_filter_initialized = true;
        
        /* 清除历史状态，避免从上一次跟踪遗留的状态影响 */
        s_error_x_sum = 0;
        s_error_y_sum = 0;
        s_error_x_prev = 0;
        s_error_y_prev = 0;
        s_filtered_out_yaw = 0;
        s_filtered_out_pitch = 0;
        s_last_gimbal_delta_yaw = 0;
        s_last_gimbal_delta_pitch = 0;
        s_compensated_vx = 0;
        s_compensated_vy = 0;
        
        change_state(TRACKER_STATE_TRACKING);
        printf("[Tracker] Locked track_id=%u at (%d,%d)\r\n",
               s_current_track_id, target_cx, target_cy);
        break;
        
    case TRACKER_STATE_TRACKING:
        /* 跟踪状态：只接受相同 track_id 的目标 */
        if (target->track_id == s_current_track_id || s_current_track_id == 0) {
            /* 
             * 位置跳变检测：多重判断
             * 1. 位置变化大且速度小 → 误关联
             * 2. 位置变化方向和速度方向不一致 → 误关联
             * 3. 单帧位置变化超过最大阈值 → 直接判定为跳变
             */
            int dx = target_cx - s_target_cx;
            int dy = target_cy - s_target_cy;
            int abs_dx = (dx > 0) ? dx : -dx;
            int abs_dy = (dy > 0) ? dy : -dy;
            int jump_dist = abs_dx + abs_dy;  /* 使用曼哈顿距离，更直观 */
            
            /* 检查 DSP 报告的速度是否能解释这个跳变 */
            int vx = target->vx;
            int vy = target->vy;
            int abs_vx = (vx > 0) ? vx : -vx;
            int abs_vy = (vy > 0) ? vy : -vy;
            int velocity = abs_vx + abs_vy;  /* 速度的曼哈顿距离 */
            
            bool is_jump = false;
            
            /* 
             * 跳变检测逻辑（使用曼哈顿距离，更直观）：
             * - 超过 100 像素：直接判定为跳变
             * - 50-100 像素：如果速度 < 20，判定为跳变
             * - 30-50 像素：如果位置和速度方向不一致，判定为跳变
             */
            
            /* 检查0: 超大跳变直接忽略 */
            if (jump_dist > 100) {
                is_jump = true;
            }
            
            /* 检查1: 位置变化大但速度小 */
            if (!is_jump && jump_dist > POSITION_JUMP_THRESHOLD && velocity < 20) {
                is_jump = true;
            }
            
            /* 检查2: 位置变化方向和速度方向严重不一致 (中等跳变时) */
            if (!is_jump && jump_dist > 30) {
                if ((dx < -20 && vx > 5) || (dx > 20 && vx < -5) ||
                    (dy < -20 && vy > 5) || (dy > 20 && vy < -5)) {
                    is_jump = true;
                }
            }
            
            if (is_jump) {
                /* 位置跳变，忽略此帧 */
                printf("[Tracker] Jump ignored: (%d,%d)->(%d,%d) d=%d v=%d\r\n",
                       s_target_cx, s_target_cy, target_cx, target_cy, jump_dist, velocity);
                /* 保持 s_last_target_time 不更新，连续跳变会触发超时 */
                break;
            }
            
            /* track_id 匹配且位置合理，正常更新 */
            s_target_cx = target_cx;
            s_target_cy = target_cy;
            s_target_valid = true;
            s_last_target_time = now;
            
            /* 更新速度信息和边缘标记 */
            s_target_vx = target->vx;
            s_target_vy = target->vy;
            s_kf_confidence = target->kf_confidence;
            s_last_edge_flags = target->edge_flags;
        }
        /* 不匹配的 track_id 被忽略 */
        break;
        
    case TRACKER_STATE_PREDICTING:
        /* 预测状态：如果锁定目标重新出现，恢复跟踪 */
        if (target->track_id == s_current_track_id) {
            /* 
             * 恢复前检查位置跳变：
             * 对比预测位置和新检测位置，跳变太大则拒绝恢复
             * 使用曼哈顿距离，与其他检查保持一致
             */
            int dx = target_cx - s_predict_cx;
            int dy = target_cy - s_predict_cy;
            int abs_dx = (dx > 0) ? dx : -dx;
            int abs_dy = (dy > 0) ? dy : -dy;
            int jump_dist = abs_dx + abs_dy;
            int max_recover_jump = 50;  /* 恢复时最大允许跳变 50 像素（曼哈顿距离）*/
            
            if (jump_dist > max_recover_jump) {
                /* 位置跳变太大，不恢复，继续等待 */
                printf("[Tracker] Recover rejected: predict(%d,%d)->detect(%d,%d) d=%d\r\n",
                       s_predict_cx, s_predict_cy, target_cx, target_cy, jump_dist);
                break;
            }
            
            /* 目标回来了！恢复跟踪 */
            s_target_cx = target_cx;
            s_target_cy = target_cy;
            s_target_valid = true;
            s_last_target_time = now;
            
            s_target_vx = target->vx;
            s_target_vy = target->vy;
            s_kf_confidence = target->kf_confidence;
            
            /* 恢复时清除云台运动记录，因为预测期间没有控制 */
            s_last_gimbal_delta_yaw = 0;
            s_last_gimbal_delta_pitch = 0;
            s_compensated_vx = 0;
            s_compensated_vy = 0;
            
            change_state(TRACKER_STATE_TRACKING);
            printf("[Tracker] Target recovered at (%d,%d) after %d predict frames\r\n",
                   target_cx, target_cy, s_predict_count);
        }
        /* 其他目标在预测期间被忽略 */
        break;
        
    case TRACKER_STATE_LOST:
    {
        /* 
         * 丢失状态目标选择策略：
         * 1. 原 track_id 回来：短冷却后立刻恢复，但需检查位置一致性
         * 2. 新 track_id：长冷却 + 位置检查 + 边缘方向匹配
         */
        uint32_t elapsed = now - s_state_enter_time;
        bool is_same_id = (target->track_id == s_lost_track_id && s_lost_track_id != 0);
        bool accept = false;
        
        /* 计算位置偏差 */
        int dx = target_cx - s_lost_cx;
        int dy = target_cy - s_lost_cy;
        int dist = (dx > 0 ? dx : -dx) + (dy > 0 ? dy : -dy);  /* 曼哈顿距离 */
        
        if (is_same_id && elapsed > LOST_COOLDOWN_SAME_ID_MS) {
            /* 原 ID 回来了，检查位置一致性 */
            if (dist <= SAME_ID_MAX_POSITION_DIFF) {
                /* 位置接近，接受 */
                accept = true;
            } else {
                /* 位置差异太大，可能是 DSP 误分配了相同 ID */
                /* 只在第一次拒绝时打印，避免刷屏 */
                static uint8_t s_last_rejected_id = 0;
                static int s_last_rejected_dist = 0;
                if (target->track_id != s_last_rejected_id || dist != s_last_rejected_dist) {
                    printf("[Tracker] Reject same id=%u: pos diff=%d (lost=(%d,%d) now=(%d,%d))\r\n",
                           target->track_id, dist, s_lost_cx, s_lost_cy, target_cx, target_cy);
                    s_last_rejected_id = target->track_id;
                    s_last_rejected_dist = dist;
                }
                /* 降级为新 ID 处理 */
                is_same_id = false;
            }
        }
        
        if (!is_same_id && elapsed > LOST_COOLDOWN_NEW_ID_MS) {
            /* 
             * 新 ID 需要：
             * 1. 位置检查：如果距离丢失位置太远，可能不是同一个人
             * 2. 边缘方向匹配
             * 
             * 但如果超过 1 秒，放宽位置限制（重新寻找任意目标）
             */
            bool position_ok = true;
            bool edge_match = true;
            
            /* 1秒内需要位置检查，1秒后可重新寻找任意目标 */
            if (elapsed < LOST_REACQUIRE_MS) {
                /* 新 ID 也需要位置接近丢失位置 */
                #define NEW_ID_MAX_POSITION_DIFF  60  /* 新 ID 允许稍大的偏差 */
                if (dist > NEW_ID_MAX_POSITION_DIFF) {
                    position_ok = false;
                }
            }
            
            /* 边缘方向匹配检查 */
            if (s_lost_edge_flags != 0 && target->edge_flags != 0) {
                bool left_exit = (s_lost_edge_flags & 0x01);
                bool right_exit = (s_lost_edge_flags & 0x02);
                bool left_enter = (target->edge_flags & 0x01);
                bool right_enter = (target->edge_flags & 0x02);
                
                if ((left_exit && right_enter) || (right_exit && left_enter)) {
                    edge_match = false;
                }
            }
            
            if (position_ok && edge_match) {
                accept = true;
            }
        }
        
        if (accept) {
            /* 接受目标 */
            s_current_track_id = target->track_id;
            s_target_cx = target_cx;
            s_target_cy = target_cy;
            s_target_valid = true;
            s_last_target_time = now;
            
            s_target_vx = target->vx;
            s_target_vy = target->vy;
            s_kf_confidence = target->kf_confidence;
            s_last_edge_flags = target->edge_flags;
            
            /* 重新初始化跟踪 */
            s_tracking = true;
            s_first_move = true;
            s_filtered_cx = (float)target_cx;
            s_filtered_cy = (float)target_cy;
            s_filter_initialized = true;
            
            /* 清除 PID 积分和输出滤波状态 */
            s_error_x_sum = 0;
            s_error_y_sum = 0;
            s_error_x_prev = 0;
            s_error_y_prev = 0;
            s_filtered_out_yaw = 0;
            s_filtered_out_pitch = 0;
            
            /* 清除云台运动记录，避免速度补偿错误 */
            s_last_gimbal_delta_yaw = 0;
            s_last_gimbal_delta_pitch = 0;
            s_compensated_vx = 0;
            s_compensated_vy = 0;
            
            change_state(TRACKER_STATE_TRACKING);
            printf("[Tracker] %s target: track_id=%u at (%d,%d)\r\n",
                   is_same_id ? "Recovered" : "New",
                   s_current_track_id, target_cx, target_cy);
        }
        break;
    }
    }
}

void tracker_poll(void)
{
    if (!s_enabled) return;
    
    uint32_t now = millis();
    
    /* 检查目标超时（备用机制，针对 TRACKING 状态） */
    if (s_state == TRACKER_STATE_TRACKING && 
        s_target_valid && (now - s_last_target_time > TARGET_LOST_TIMEOUT_MS)) {
        /* 超时，进入预测模式 */
        change_state(TRACKER_STATE_PREDICTING);
        s_predict_count = 0;
        s_predict_cx = s_target_cx;
        s_predict_cy = s_target_cy;
        /* 保存速度并限制上限，防止预测位置飞出画面 */
        s_last_vx = s_target_vx;
        s_last_vy = s_target_vy;
        if (s_last_vx > MAX_PREDICT_VELOCITY) s_last_vx = MAX_PREDICT_VELOCITY;
        if (s_last_vx < -MAX_PREDICT_VELOCITY) s_last_vx = -MAX_PREDICT_VELOCITY;
        /* 不再预测移动，速度清零 */
        s_last_vx = 0;
        s_last_vy = 0;
    }
    
    /* 控制周期检查 - 必须在预测逻辑之前！ */
    if (now - s_last_control_time < CONTROL_PERIOD_MS) return;
    s_last_control_time = now;
    
    /* 统计 poll 次数（过了周期检查的） */
    s_debug_poll_count++;
    
    /*
     * 简化的预测处理：目标消失时不再预测移动，只是等待一段时间
     * 如果目标重新出现则恢复跟踪，否则判定丢失
     */
    if (s_state == TRACKER_STATE_PREDICTING) {
        s_predict_count++;
        
        if (s_predict_count <= MAX_PREDICT_FRAMES) {
            /* 等待期间不控制云台，只打印状态 */
            s_target_valid = false;
            
            if (s_predict_count == 1 || s_predict_count == MAX_PREDICT_FRAMES) {
                printf("[Tracker] Waiting for target [%d/%d]\r\n",
                       s_predict_count, MAX_PREDICT_FRAMES);
            }
        } else {
            /* 等待超时，确认丢失 */
            s_target_valid = false;
            s_tracking = false;
            s_filter_initialized = false;
            s_first_move = true;
            
            /* 记录丢失时的 track_id、边缘方向和位置，用于后续优先恢复 */
            s_lost_track_id = s_current_track_id;
            s_lost_edge_flags = s_last_edge_flags;
            s_lost_cx = s_target_cx;
            s_lost_cy = s_target_cy;
            
            s_current_track_id = 0;
            s_error_x_sum = 0;
            s_error_y_sum = 0;
            
            change_state(TRACKER_STATE_LOST);
            printf("[Tracker] Target lost after %d frames (id=%u, edge=%02X)\r\n", 
                   MAX_PREDICT_FRAMES, s_lost_track_id, s_lost_edge_flags);
        }
        
        return;  /* 预测期间不执行 PID 控制 */
    }
    
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
    
    /* 
     * 渐进死区处理（软死区）：
     * - 误差 < 死区的一半: 不动
     * - 死区的一半 <= 误差 < 死区: 按比例衰减
     * - 误差 >= 死区: 正常处理
     * 这样可以避免硬截断造成的"一卡一卡"
     */
    float half_deadzone = s_deadzone * 0.5f;
    if (fabs_f(error_x) < half_deadzone) {
        error_x = 0;
    } else if (fabs_f(error_x) < s_deadzone) {
        /* 在半死区到死区之间，按比例衰减 */
        float ratio = (fabs_f(error_x) - half_deadzone) / half_deadzone;
        error_x = (error_x > 0 ? 1 : -1) * ratio * half_deadzone;
    }
    
    if (fabs_f(error_y) < half_deadzone) {
        error_y = 0;
    } else if (fabs_f(error_y) < s_deadzone) {
        float ratio = (fabs_f(error_y) - half_deadzone) / half_deadzone;
        error_y = (error_y > 0 ? 1 : -1) * ratio * half_deadzone;
    }
    
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
    
    /* 应用基于框大小的增益系数：近距离降低增益，远距离提高增益 */
    kp_x *= s_box_size_gain;
    kp_y *= s_box_size_gain;
    
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
     * 边缘保护：当目标太靠近画面边缘时，减小追踪力度
     * 避免追过头导致目标出画面
     * 边缘区域定义为距离边缘 15% 以内
     */
    {
        const float EDGE_RATIO = 0.15f;  /* 边缘区域占比 */
        const float EDGE_GAIN_MIN = 0.3f; /* 边缘时最小增益 */
        
        float edge_x_left = s_img_width * EDGE_RATIO;
        float edge_x_right = s_img_width * (1.0f - EDGE_RATIO);
        float edge_y_top = s_img_height * EDGE_RATIO;
        float edge_y_bottom = s_img_height * (1.0f - EDGE_RATIO);
        
        float edge_gain_x = 1.0f;
        float edge_gain_y = 1.0f;
        
        /* X 边缘检测 */
        if (s_filtered_cx < edge_x_left) {
            /* 目标在左边缘，按距离衰减 */
            edge_gain_x = EDGE_GAIN_MIN + (1.0f - EDGE_GAIN_MIN) * (s_filtered_cx / edge_x_left);
        } else if (s_filtered_cx > edge_x_right) {
            /* 目标在右边缘 */
            edge_gain_x = EDGE_GAIN_MIN + (1.0f - EDGE_GAIN_MIN) * 
                          ((s_img_width - s_filtered_cx) / (s_img_width * EDGE_RATIO));
        }
        
        /* Y 边缘检测 */
        if (s_filtered_cy < edge_y_top) {
            edge_gain_y = EDGE_GAIN_MIN + (1.0f - EDGE_GAIN_MIN) * (s_filtered_cy / edge_y_top);
        } else if (s_filtered_cy > edge_y_bottom) {
            edge_gain_y = EDGE_GAIN_MIN + (1.0f - EDGE_GAIN_MIN) * 
                          ((s_img_height - s_filtered_cy) / (s_img_height * EDGE_RATIO));
        }
        
        /* 限制增益范围 */
        edge_gain_x = clamp(edge_gain_x, EDGE_GAIN_MIN, 1.0f);
        edge_gain_y = clamp(edge_gain_y, EDGE_GAIN_MIN, 1.0f);
        
        out_x *= edge_gain_x;
        out_y *= edge_gain_y;
    }
    
    /* 
     * 坐标映射 (需要反转方向):
     * - 目标在画面右边 (error_x > 0) -> 云台应向左转追踪 (delta_yaw < 0)
     * - 目标在画面上方 (error_y < 0) -> 云台应向上抬追踪 (delta_pitch > 0)
     *   因为 pitch: 0°=朝上, -90°=水平，向上抬需要 pitch 增大
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
     * 第一帧限制：ID切换后的第一次输出限制到较小值
     * 避免从一个目标切换到另一个目标时的突然大幅移动
     */
    if (s_first_move) {
        s_first_move = false;
        /* 第一帧限制到最大 1.5 度，让滤波器逐渐追上 */
        const float FIRST_MOVE_LIMIT = 1.5f;
        if (delta_yaw > FIRST_MOVE_LIMIT) delta_yaw = FIRST_MOVE_LIMIT;
        else if (delta_yaw < -FIRST_MOVE_LIMIT) delta_yaw = -FIRST_MOVE_LIMIT;
        if (delta_pitch > FIRST_MOVE_LIMIT) delta_pitch = FIRST_MOVE_LIMIT;
        else if (delta_pitch < -FIRST_MOVE_LIMIT) delta_pitch = -FIRST_MOVE_LIMIT;
        printf("[Tracker] First move limited to (%.1f,%.1f)deg\r\n", delta_yaw, delta_pitch);
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
        
        printf("[Tracker] id=%u pos=(%d,%d) err=(%.0f,%.0f) out=(%.1f,%.1f) box=%.0f%% gain=%.2f\r\n",
               s_current_track_id,
               s_target_cx, s_target_cy,
               s_debug_last_error_x, s_debug_last_error_y,
               s_debug_last_delta_yaw, s_debug_last_delta_pitch,
               s_box_size_ratio * 100.0f, s_box_size_gain);
        
        /* 始终打印速度信息，便于调试 DSP 速度估计 */
        printf("[Tracker] vel: raw=(%d,%d) comp=(%.1f,%.1f) conf=%u jitter=(%.0f%%,%.0f%%)\r\n",
               s_target_vx, s_target_vy,
               s_compensated_vx, s_compensated_vy,
               s_kf_confidence,
               jitter_yaw, jitter_pitch);
        
        /* 打印云台运动产生的假速度，帮助分析 DSP 速度是否合理 */
        float gimbal_fake_vx = (s_last_gimbal_delta_yaw / CAMERA_FOV_X_DEG) * s_img_width;
        float gimbal_fake_vy = (s_last_gimbal_delta_pitch / CAMERA_FOV_Y_DEG) * s_img_height;
        printf("[Tracker] gimbal: delta=(%.2f,%.2f)deg fake_vel=(%.1f,%.1f)px\r\n",
               s_last_gimbal_delta_yaw, s_last_gimbal_delta_pitch,
               gimbal_fake_vx, gimbal_fake_vy);
        
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
        s_current_track_id = 0;
        s_state = TRACKER_STATE_IDLE;
        s_state_enter_time = 0;
        s_predict_count = 0;
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
    return (s_state == TRACKER_STATE_TRACKING || s_state == TRACKER_STATE_PREDICTING) 
           ? s_current_track_id : 0;
}

void tracker_get_last_position(int16_t *out_cx, int16_t *out_cy)
{
    if (out_cx) *out_cx = (int16_t)s_target_cx;
    if (out_cy) *out_cy = (int16_t)s_target_cy;
}

bool tracker_is_lost(int16_t *out_cx, int16_t *out_cy)
{
    if (s_state == TRACKER_STATE_LOST) {
        if (out_cx) *out_cx = (int16_t)s_lost_cx;
        if (out_cy) *out_cy = (int16_t)s_lost_cy;
        return true;
    }
    return false;
}

uint32_t tracker_get_lost_duration_ms(void)
{
    if (s_state == TRACKER_STATE_LOST) {
        return millis() - s_state_enter_time;
    }
    return 0;
}

void tracker_reset(void)
{
    s_tracking = false;
    s_target_valid = false;
    s_filter_initialized = false;
    s_first_move = true;
    s_error_x_sum = 0;
    s_error_y_sum = 0;
    s_current_track_id = 0;
    s_state = TRACKER_STATE_IDLE;
    s_state_enter_time = 0;
    s_predict_count = 0;
    s_predict_cx = 0;
    s_predict_cy = 0;
    s_last_vx = 0;
    s_last_vy = 0;
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
