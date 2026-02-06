/**
 * @file    imu.c
 * @brief   IMU 姿态解算实现
 * @details 使用互补滤波算法进行姿态解算
 *          核心代码参考自: https://github.com/Krasjet/quaternion
 * 
 * @note    参考正点原子SDK实现
 */

#include "imu.h"
#include <math.h>
#include <string.h>

/*===========================================================================
 * 私有函数声明
 *===========================================================================*/

static float inv_sqrt(float x);
static void compute_rotation_matrix(imu_t *imu);
static void calibrate_gravity(imu_t *imu, float *acc);

/*===========================================================================
 * 私有函数实现
 *===========================================================================*/

/**
 * @brief 快速平方根倒数
 */
static float inv_sqrt(float x)
{
    return 1.0f / sqrtf(x);
}

/**
 * @brief 计算旋转矩阵
 */
static void compute_rotation_matrix(imu_t *imu)
{
    float q0 = imu->quat.w;
    float q1 = imu->quat.x;
    float q2 = imu->quat.y;
    float q3 = imu->quat.z;
    
    float q1q1 = q1 * q1;
    float q2q2 = q2 * q2;
    float q3q3 = q3 * q3;
    float q0q1 = q0 * q1;
    float q0q2 = q0 * q2;
    float q0q3 = q0 * q3;
    float q1q2 = q1 * q2;
    float q1q3 = q1 * q3;
    float q2q3 = q2 * q3;

    imu->rot_mat[0][0] = 1.0f - 2.0f * (q2q2 + q3q3);
    imu->rot_mat[0][1] = 2.0f * (q1q2 - q0q3);
    imu->rot_mat[0][2] = 2.0f * (q1q3 + q0q2);

    imu->rot_mat[1][0] = 2.0f * (q1q2 + q0q3);
    imu->rot_mat[1][1] = 1.0f - 2.0f * (q1q1 + q3q3);
    imu->rot_mat[1][2] = 2.0f * (q2q3 - q0q1);

    imu->rot_mat[2][0] = 2.0f * (q1q3 - q0q2);
    imu->rot_mat[2][1] = 2.0f * (q2q3 + q0q1);
    imu->rot_mat[2][2] = 1.0f - 2.0f * (q1q1 + q2q2);
}

/**
 * @brief 重力校准
 */
static void calibrate_gravity(imu_t *imu, float *acc)
{
    static uint16_t cnt = 0;
    static float acc_z_min = 1.5f, acc_z_max = 0.5f;
    static float sum_acc[3] = {0.0f};

    if (cnt == 0) {
        acc_z_min = acc[2];
        acc_z_max = acc[2];
        for (int i = 0; i < 3; i++) {
            sum_acc[i] = 0.0f;
        }
    }

    for (int i = 0; i < 3; i++) {
        sum_acc[i] += acc[i];
    }

    if (acc[2] < acc_z_min) acc_z_min = acc[2];
    if (acc[2] > acc_z_max) acc_z_max = acc[2];

    if (++cnt >= IMU_ACCZ_SAMPLE) {
        cnt = 0;
        imu->max_error = acc_z_max - acc_z_min;

        if (imu->max_error < 100.0f) {
            for (int i = 0; i < 3; i++) {
                imu->base_acc[i] = sum_acc[i] / IMU_ACCZ_SAMPLE;
            }
            imu->gravity_calibrated = true;
        }
        for (int i = 0; i < 3; i++) {
            sum_acc[i] = 0.0f;
        }
    }
}

/*===========================================================================
 * 公共 API 实现
 *===========================================================================*/

void imu_init(imu_t *imu)
{
    memset(imu, 0, sizeof(imu_t));
    
    /* 初始化四元数为单位四元数 */
    imu->quat.w = 1.0f;
    imu->quat.x = 0.0f;
    imu->quat.y = 0.0f;
    imu->quat.z = 0.0f;
    
    /* 默认PI控制器参数 */
    imu->kp = 10.0f;
    imu->ki = 0.05f;
    
    /* 初始化积分误差 */
    imu->ex_int = 0.0f;
    imu->ey_int = 0.0f;
    imu->ez_int = 0.0f;
    
    /* 重力校准状态 */
    imu->gravity_calibrated = false;
    imu->base_acc[0] = 0.0f;
    imu->base_acc[1] = 0.0f;
    imu->base_acc[2] = 1.0f;
    
    /* 计算初始旋转矩阵 */
    compute_rotation_matrix(imu);
}

void imu_set_pid(imu_t *imu, float kp, float ki)
{
    imu->kp = kp;
    imu->ki = ki;
}

void imu_reset(imu_t *imu)
{
    imu_init(imu);
}

imu_euler_t* imu_update(imu_t *imu, float acc[3], float gyr[3], float dt)
{
    float normalise;
    float ex, ey, ez;
    float half_t = 0.5f * dt;
    float acc_buf[3] = {0.0f};
    
    /* 如果加速度计有效，进行融合 */
    if (acc[0] != 0.0f || acc[1] != 0.0f || acc[2] != 0.0f) {
        compute_rotation_matrix(imu);
        
        /* 单位化加速度计测量值 */
        normalise = inv_sqrt(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
        acc[0] *= normalise;
        acc[1] *= normalise;
        acc[2] *= normalise;
        
        /* 加速度计读取方向与重力加速度方向的差值 (叉积) */
        ex = acc[1] * imu->rot_mat[2][2] - acc[2] * imu->rot_mat[2][1];
        ey = acc[2] * imu->rot_mat[2][0] - acc[0] * imu->rot_mat[2][2];
        ez = acc[0] * imu->rot_mat[2][1] - acc[1] * imu->rot_mat[2][0];
        
        /* 误差累计积分 */
        imu->ex_int += imu->ki * ex * dt;
        imu->ey_int += imu->ki * ey * dt;
        imu->ez_int += imu->ki * ez * dt;
        
        /* 用叉积误差做PI修正陀螺零偏 */
        gyr[0] += imu->kp * ex + imu->ex_int;
        gyr[1] += imu->kp * ey + imu->ey_int;
        gyr[2] += imu->kp * ez + imu->ez_int;
    }
    
    /* 一阶近似算法，四元数运动学方程离散化 */
    float q0_last = imu->quat.w;
    float q1_last = imu->quat.x;
    float q2_last = imu->quat.y;
    float q3_last = imu->quat.z;
    
    imu->quat.w += (-q1_last * gyr[0] - q2_last * gyr[1] - q3_last * gyr[2]) * half_t;
    imu->quat.x += ( q0_last * gyr[0] + q2_last * gyr[2] - q3_last * gyr[1]) * half_t;
    imu->quat.y += ( q0_last * gyr[1] - q1_last * gyr[2] + q3_last * gyr[0]) * half_t;
    imu->quat.z += ( q0_last * gyr[2] + q1_last * gyr[1] - q2_last * gyr[0]) * half_t;
    
    /* 单位化四元数 */
    normalise = inv_sqrt(imu->quat.w * imu->quat.w + 
                         imu->quat.x * imu->quat.x + 
                         imu->quat.y * imu->quat.y + 
                         imu->quat.z * imu->quat.z);
    imu->quat.w *= normalise;
    imu->quat.x *= normalise;
    imu->quat.y *= normalise;
    imu->quat.z *= normalise;
    
    /* 更新旋转矩阵 */
    compute_rotation_matrix(imu);
    
    /* 计算欧拉角 (度) */
    imu->euler.pitch = -asinf(imu->rot_mat[2][0]) * IMU_RAD2DEG;
    imu->euler.roll  = -atan2f(imu->rot_mat[2][1], imu->rot_mat[2][2]) * IMU_RAD2DEG;
    imu->euler.yaw   = -atan2f(imu->rot_mat[1][0], imu->rot_mat[0][0]) * IMU_RAD2DEG;
    
    /* 重力校准 */
    if (!imu->gravity_calibrated) {
        acc_buf[2] = acc[0] * imu->rot_mat[2][0] + 
                     acc[1] * imu->rot_mat[2][1] + 
                     acc[2] * imu->rot_mat[2][2];
        calibrate_gravity(imu, acc_buf);
    }
    
    return &imu->euler;
}

imu_quat_t* imu_get_quaternion(imu_t *imu)
{
    return &imu->quat;
}

imu_euler_t* imu_get_euler(imu_t *imu)
{
    return &imu->euler;
}

bool imu_is_calibrated(imu_t *imu)
{
    return imu->gravity_calibrated;
}
