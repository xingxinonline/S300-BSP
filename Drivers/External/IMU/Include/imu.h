/**
 * @file    imu.h
 * @brief   IMU 姿态解算头文件
 * @details 使用互补滤波算法进行姿态解算
 *          核心代码参考自: https://github.com/Krasjet/quaternion
 * 
 * @note    参考正点原子SDK实现
 */

#ifndef S300_BSP_IMU_H
#define S300_BSP_IMU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/*===========================================================================
 * 宏定义
 *===========================================================================*/

/** @brief 默认采样周期 (秒) */
#define IMU_DEFAULT_DT      0.01f

/** @brief 度转弧度 π/180 */
#define IMU_DEG2RAD         0.017453293f

/** @brief 弧度转度 180/π */
#define IMU_RAD2DEG         57.29578f

/** @brief 重力校准采样次数 */
#define IMU_ACCZ_SAMPLE     80

/*===========================================================================
 * 数据结构
 *===========================================================================*/

/**
 * @brief 欧拉角结构体 (单位: 度)
 */
typedef struct {
    float roll;     /**< 横滚角 (绕X轴) */
    float pitch;    /**< 俯仰角 (绕Y轴) */
    float yaw;      /**< 偏航角 (绕Z轴) */
} imu_euler_t;

/**
 * @brief 四元数结构体
 */
typedef struct {
    float w;
    float x;
    float y;
    float z;
} imu_quat_t;

/**
 * @brief IMU 姿态解算句柄
 */
typedef struct {
    /* 四元数 */
    imu_quat_t quat;
    
    /* 旋转矩阵 (3x3) */
    float rot_mat[3][3];
    
    /* PI控制器参数 */
    float kp;               /**< 比例增益 */
    float ki;               /**< 积分增益 */
    float ex_int;           /**< X轴积分误差 */
    float ey_int;           /**< Y轴积分误差 */
    float ez_int;           /**< Z轴积分误差 */
    
    /* 重力校准 */
    bool gravity_calibrated;
    float base_acc[3];
    float max_error;
    
    /* 欧拉角输出 */
    imu_euler_t euler;
} imu_t;

/*===========================================================================
 * API 函数声明
 *===========================================================================*/

/**
 * @brief   初始化 IMU 姿态解算
 * @param   imu     IMU句柄
 */
void imu_init(imu_t *imu);

/**
 * @brief   设置 PI 控制器参数
 * @param   imu     IMU句柄
 * @param   kp      比例增益 (推荐 10.0)
 * @param   ki      积分增益 (推荐 0.05)
 */
void imu_set_pid(imu_t *imu, float kp, float ki);

/**
 * @brief   重置姿态解算状态
 * @param   imu     IMU句柄
 */
void imu_reset(imu_t *imu);

/**
 * @brief   更新姿态解算 (获取欧拉角)
 * @param   imu     IMU句柄
 * @param   acc     加速度计数据 [ax, ay, az] (m/s²)
 * @param   gyr     陀螺仪数据 [gx, gy, gz] (rad/s)
 * @param   dt      采样周期 (秒)
 * @return  指向欧拉角结构体的指针
 * 
 * @note    此函数使用互补滤波算法，融合加速度计和陀螺仪数据
 *          尽量保证调用频率稳定，否则YAW会产生偏差
 */
imu_euler_t* imu_update(imu_t *imu, float acc[3], float gyr[3], float dt);

/**
 * @brief   获取当前四元数
 * @param   imu     IMU句柄
 * @return  指向四元数结构体的指针
 */
imu_quat_t* imu_get_quaternion(imu_t *imu);

/**
 * @brief   获取当前欧拉角
 * @param   imu     IMU句柄
 * @return  指向欧拉角结构体的指针
 */
imu_euler_t* imu_get_euler(imu_t *imu);

/**
 * @brief   检查重力校准是否完成
 * @param   imu     IMU句柄
 * @return  true: 已校准, false: 未校准
 */
bool imu_is_calibrated(imu_t *imu);

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_IMU_H */
