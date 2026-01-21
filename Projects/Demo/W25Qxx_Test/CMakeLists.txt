# =============================================================================
# W25Qxx_Test Demo - SPI Flash 读写测试
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(W25Qxx_Test)

s300_add_executable(
    TARGET s300_w25qxx_test
    SOURCES Src/main.c
    DRIVERS rcc gpio uart qspi w25qxx
)
