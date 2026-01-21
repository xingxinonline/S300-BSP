# =============================================================================
# FreeRTOS_Shell Demo - FreeRTOS 命令行 Shell
# =============================================================================
# 此 Demo 使用 FreeRTOS，需要特殊的 FetchContent 配置
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(FreeRTOS_Shell)

# =============================================================================
# FreeRTOS-Kernel 配置
# =============================================================================
set(FREERTOS_KERNEL_LOCAL_PATH "" CACHE PATH "Local path to FreeRTOS-Kernel (optional)")
set(FREERTOS_PORT GCC_ARM_CM4F CACHE STRING "FreeRTOS port")
set(FREERTOS_HEAP 4 CACHE STRING "FreeRTOS heap implementation (1..5)")
set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Path to FreeRTOSConfig.h")

if(FREERTOS_KERNEL_LOCAL_PATH AND EXISTS ${FREERTOS_KERNEL_LOCAL_PATH}/CMakeLists.txt)
    message(STATUS "Using FreeRTOS-Kernel from local path: ${FREERTOS_KERNEL_LOCAL_PATH}")
    add_subdirectory(${FREERTOS_KERNEL_LOCAL_PATH} ${CMAKE_CURRENT_BINARY_DIR}/freertos-kernel-local)
else()
    include(FetchContent)
    FetchContent_Declare(
        freertos_kernel
        GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
        GIT_TAG V10.5.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(freertos_kernel)
endif()

# FreeRTOS kernel 需要 CMSIS include (FreeRTOSConfig.h 依赖 s300.h)
target_include_directories(freertos_kernel PUBLIC
    ${CMAKE_SOURCE_DIR}/CMSIS/Device/PiMCHIP/S300/Include
    ${CMAKE_SOURCE_DIR}/CMSIS/Core/Include
)

# =============================================================================
# 项目配置
# =============================================================================
s300_add_executable(
    TARGET s300_freertos_shell
    SOURCES Src/main.c
    DRIVERS rcc gpio uart
    INCLUDES .
    LIBRARIES freertos_kernel
    NO_IMAGE
)

# FreeRTOS 项目需要单独添加镜像目标（因为依赖特殊）
add_s300_image(s300_freertos_shell)

# 调试别名
add_custom_target(dbg_freertos
    COMMENT "Alias: run FreeRTOS shell debug"
    DEPENDS dbg_freertos_shell
)
