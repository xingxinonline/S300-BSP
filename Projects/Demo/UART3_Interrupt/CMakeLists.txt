# =============================================================================
# UART3_Interrupt Demo - UART3 中断接收演示
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(UART3_Interrupt)

s300_add_executable(
    TARGET s300_uart3_interrupt
    SOURCES Src/main.c
    DRIVERS rcc gpio uart
)
