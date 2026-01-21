# =============================================================================
# DMA_UART3_Rx Demo - DMA 驱动的 UART3 接收演示
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(DMA_UART3_Rx)

s300_add_executable(
    TARGET s300_dma_uart3_rx
    SOURCES Src/main.c
    DRIVERS rcc gpio uart dma
)
