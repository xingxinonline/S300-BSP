# =============================================================================
# OV5640_I2C1_Init Demo - 摄像头 I2C 初始化测试
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(OV5640_I2C1_Init)
# mm -> ov5640 -> i2c_soft -> gpio -> rcc (自动解析)
s300_add_executable(
    TARGET s300_ov5640_i2c1_init
    SOURCES Src/main.c
    DRIVERS uart mm
    DBG_NAME ov5640
    MM_ENABLE
)
