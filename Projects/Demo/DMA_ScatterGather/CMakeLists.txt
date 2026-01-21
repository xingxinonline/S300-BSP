# =============================================================================
# DMA_ScatterGather Demo - DMA 分散聚合传输演示
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(DMA_ScatterGather)

s300_add_executable(
    TARGET s300_dma_scatter_gather
    SOURCES Src/main.c
    DRIVERS rcc gpio uart dma
)
