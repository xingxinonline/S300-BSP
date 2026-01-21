# =============================================================================
# App_HelloWorld - 最小示例应用
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(App_HelloWorld)

s300_add_executable(
    TARGET s300_hello_world
    SOURCES Src/main.c
    DRIVERS rcc gpio uart
)
