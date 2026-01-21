# =============================================================================
# DMA_Interrupt Demo - DMA 中断演示
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(DMA_Interrupt)

s300_add_executable(
    TARGET s300_dma_interrupt
    SOURCES Src/main.c
    DRIVERS rcc gpio uart dma
)
