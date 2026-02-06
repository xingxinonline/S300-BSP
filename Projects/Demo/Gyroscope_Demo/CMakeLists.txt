# =============================================================================
# Gyroscope_Demo - 陀螺仪/IMU 自稳云台演示
# =============================================================================
# 演示 QMI8658A 六轴IMU传感器配合总线舵机实现自稳
# 读取加速度计和陀螺仪数据，进行姿态解算，驱动舵机保持垂直
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(Gyroscope_Demo)

# 添加总线舵机驱动
add_subdirectory(${CMAKE_SOURCE_DIR}/Drivers/External/BusServo ${CMAKE_CURRENT_BINARY_DIR}/BusServo)

# qmi8658a -> i2c_soft -> gpio -> rcc (自动解析)
# imu 姿态解算库
# bus_servo 总线舵机驱动
s300_add_executable(
    TARGET s300_gyroscope_demo
    SOURCES Src/main.c
    DRIVERS uart qmi8658a imu
    LIBRARIES bus_servo m
    DBG_NAME gyro
)
