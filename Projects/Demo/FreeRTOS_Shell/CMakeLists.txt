cmake_minimum_required(VERSION 3.16)

project(FreeRTOS_Shell)

# Target
set(TARGET s300_freertos_shell)

# Paths
set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ROOT ${PROJECT_DIR}/../../..)
set(DEVICE_DIR ${ROOT}/CMSIS/Device/PiMCHIP/S300)
set(LD_SCRIPT ${ROOT}/ld/sram.ld)

# Includes
include_directories(
    ${DEVICE_DIR}/Include
    ${ROOT}/CMSIS/Core/Include
    ${ROOT}/Drivers/SoC/RCC/Include
    ${ROOT}/Drivers/SoC/GPIO/Include
    ${ROOT}/Drivers/SoC/UART/Include
    ${PROJECT_DIR} # for FreeRTOSConfig.h
)

# Sources (board files provided by s300_board library)
set(SRCS
    ${DEVICE_DIR}/Source/GCC/startup_S300.S
    ${ROOT}/CMSIS/Device/PiMCHIP/S300/Source/system_S300.c
    ${ROOT}/Drivers/SoC/RCC/Source/rcc.c
    ${ROOT}/Drivers/SoC/GPIO/Source/gpio.c
    ${ROOT}/Drivers/SoC/UART/Source/uart.c
    ${PROJECT_DIR}/Src/main.c
)

set_source_files_properties(${DEVICE_DIR}/Source/GCC/startup_S300.S PROPERTIES LANGUAGE ASM)

add_executable(${TARGET} ${SRCS})

# FreeRTOS-Kernel integration: prefer local path if provided; otherwise use FetchContent
set(FREERTOS_KERNEL_LOCAL_PATH "" CACHE PATH "Local path to FreeRTOS-Kernel (optional)")
set(FREERTOS_PORT GCC_ARM_CM4F CACHE STRING "FreeRTOS port")
set(FREERTOS_HEAP 4 CACHE STRING "FreeRTOS heap implementation (1..5)")
set(FREERTOS_CONFIG_FILE_DIRECTORY ${PROJECT_DIR} CACHE PATH "Path to FreeRTOSConfig.h")

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

# Link board support library and FreeRTOS
target_link_libraries(${TARGET} PRIVATE s300_board freertos_kernel)

# Linker script
set_target_properties(${TARGET} PROPERTIES LINK_FLAGS "-T${LD_SCRIPT}")

# Post build artifacts (bin/hex/disasm/size)
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bin
    COMMAND arm-none-eabi-objcopy -O ihex $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.hex
    COMMAND arm-none-eabi-objdump -d -S -C -M force-thumb,reg-names-std -j .text $<TARGET_FILE:${TARGET}> > ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.dis
    COMMAND arm-none-eabi-size $<TARGET_FILE:${TARGET}>
    COMMENT "Generating binary, hex, disassembly and size info for ${TARGET}"
)

# Debug target
set(GDB_PORT 3333 CACHE STRING "GDB server port")
add_custom_target(dbg_freertos_shell
    COMMENT "Launching GDB for ${TARGET} (SRAM: 0x20000000) (GDB_PORT=${GDB_PORT})"
    COMMAND arm-none-eabi-gdb -q
    -ex "file $<TARGET_FILE:${TARGET}>"
    -ex "target extended-remote :${GDB_PORT}"
    -x ${ROOT}/gdbinit.gdb
    DEPENDS ${TARGET}
    USES_TERMINAL
)# Alias target for convenience
add_custom_target(dbg_freertos
    COMMENT "Alias: run FreeRTOS shell debug"
    DEPENDS dbg_freertos_shell
)
