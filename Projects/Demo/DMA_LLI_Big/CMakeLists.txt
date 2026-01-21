# =============================================================================
# DMA_LLI_Big Demo - DMA 链表大块传输演示
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(DMA_LLI_Big)

s300_add_executable(
    TARGET s300_dma_lli_big
    SOURCES Src/main.c
    DRIVERS rcc gpio uart dma
)
