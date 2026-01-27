# =============================================================================
# BusServo_Demo - 总线舵机控制演示
# =============================================================================
# 演示 Hiwonder 总线舵机的各种控制功能
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(BusServo_Demo)

# 添加总线舵机驱动
add_subdirectory(${CMAKE_SOURCE_DIR}/Drivers/External/BusServo ${CMAKE_CURRENT_BINARY_DIR}/BusServo)

# 调试测试程序 (切换 main.c 和 main_test.c)
# ON: 使用 main_test.c (独立调试程序)
# OFF: 使用 main.c (正式两轴云台演示)
set(USE_TEST_MAIN OFF)

if(USE_TEST_MAIN)
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
