# =============================================================================
# DMA_UART3_Tx Demo - DMA 驱动的 UART3 发送演示
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(DMA_UART3_Tx)

s300_add_executable(
    TARGET s300_dma_uart3_tx
    SOURCES Src/main.c
    DRIVERS rcc gpio uart dma
)
