# =============================================================================
# DMA_Ram2Ram Demo - DMA 内存到内存传输
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(DMA_Ram2Ram)

s300_add_executable(
    TARGET s300_dma_ram2ram
    SOURCES Src/main.c
    DRIVERS rcc gpio uart dma
)
