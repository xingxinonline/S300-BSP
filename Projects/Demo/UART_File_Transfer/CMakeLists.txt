# =============================================================================
# UART_File_Transfer Demo - UART 文件传输
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(UART_File_Transfer)

s300_add_executable(
    TARGET s300_uart_file_transfer
    SOURCES Src/main.c
    DRIVERS rcc gpio uart dma qspi w25qxx
    LIBRARIES m
)
