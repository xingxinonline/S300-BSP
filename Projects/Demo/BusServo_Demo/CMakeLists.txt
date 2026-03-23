# =============================================================================
# BusServo_Demo - 总线舵机控制演示
# =============================================================================
# 演示 Hiwonder 总线舵机的各种控制功能
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(BusServo_Demo)

# 添加总线舵机驱动
add_subdirectory(${CMAKE_SOURCE_DIR}/Drivers/External/BusServo ${CMAKE_CURRENT_BINARY_DIR}/BusServo)

# 调试测试程序入口选择
# BUS_SERVO_POS_READ_DIAG_MAIN=ON: 极简位置读诊断
# USE_TEST_MAIN=ON: 通用独立测试程序
# BUS_SERVO_SDK_READONLY_MAIN=ON: SDK 风格只读测试程序
# 默认: 正式两轴云台演示
set(USE_TEST_MAIN "OFF" CACHE STRING "Use BusServo_Demo/Src/main_test.c: ON or OFF")
set(BUS_SERVO_POS_READ_DIAG_MAIN "OFF" CACHE STRING "Use BusServo_Demo/Src/main_pos_read_diag.c: ON or OFF")
set(BUS_SERVO_SDK_READONLY_MAIN "OFF" CACHE STRING "Use BusServo_Demo/Src/main_sdk_readonly.c: ON or OFF")

if(BUS_SERVO_POS_READ_DIAG_MAIN STREQUAL "ON")
    set(MAIN_SOURCE Src/main_pos_read_diag.c)
elseif(BUS_SERVO_SDK_READONLY_MAIN STREQUAL "ON")
    set(MAIN_SOURCE Src/main_sdk_readonly.c)
elseif(USE_TEST_MAIN)
    set(MAIN_SOURCE Src/main_test.c)
else()
    set(MAIN_SOURCE Src/main.c)
endif()

# 使用统一的项目创建函数
s300_add_executable(
    TARGET s300_bus_servo_demo
    SOURCES ${MAIN_SOURCE}
    DRIVERS rcc gpio uart
    LIBRARIES bus_servo m
)

message(STATUS "BusServo_Demo: main source = ${MAIN_SOURCE}")
