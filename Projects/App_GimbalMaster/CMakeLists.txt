# =============================================================================
# App_GimbalMaster - 云台主控应用 (MVP0)
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(App_GimbalMaster)

if(NOT TARGET ws2812)
    add_subdirectory(${CMAKE_SOURCE_DIR}/Drivers/External/WS2812 ${CMAKE_CURRENT_BINARY_DIR}/WS2812)
endif()

set(FREERTOS_KERNEL_LOCAL_PATH "" CACHE PATH "Local path to FreeRTOS-Kernel (optional)")
set(FREERTOS_PORT GCC_ARM_CM4F CACHE STRING "FreeRTOS port")
set(FREERTOS_HEAP 4 CACHE STRING "FreeRTOS heap implementation (1..5)")
set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Path to FreeRTOSConfig.h")

if(NOT TARGET freertos_kernel)
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

    target_include_directories(freertos_kernel PUBLIC
        ${CMAKE_SOURCE_DIR}/CMSIS/Device/PiMCHIP/S300/Include
        ${CMAKE_SOURCE_DIR}/CMSIS/Core/Include
    )
endif()

s300_add_executable(
    TARGET s300_gimbal_master
    SOURCES Src/main.c Src/app_log.c
    DRIVERS rcc gpio uart
    INCLUDES . Inc
    LIBRARIES freertos_kernel ws2812
    NO_IMAGE
)

add_s300_image(s300_gimbal_master)
