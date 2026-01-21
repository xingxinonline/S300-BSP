# =============================================================================
# PSRAM_Test Demo - PSRAM 读写测试
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(PSRAM_Test)

s300_add_executable(
    TARGET s300_psram_test
    SOURCES main.c
    DRIVERS rcc gpio uart psram
    DBG_NAME psram
)
