# App_HelloWorld CMakeLists.txt
# 最小示例应用 - Hello World

cmake_minimum_required(VERSION 3.16)
project(App_HelloWorld)

set(TARGET s300_hello_world)

# Paths
set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ROOT ${PROJECT_DIR}/../..)
set(DEVICE_DIR ${ROOT}/CMSIS/Device/PiMCHIP/S300)
set(LD_SCRIPT ${ROOT}/ld/sram.ld)

# Includes
include_directories(
    ${DEVICE_DIR}/Include
    ${ROOT}/CMSIS/Core/Include
    ${ROOT}/Drivers/SoC/RCC/Include
    ${ROOT}/Drivers/SoC/GPIO/Include
    ${ROOT}/Drivers/SoC/UART/Include
)

# Sources
set(SRCS
    ${DEVICE_DIR}/Source/GCC/startup_S300.S
    ${DEVICE_DIR}/Source/system_S300.c
    ${ROOT}/Drivers/SoC/RCC/Source/rcc.c
    ${ROOT}/Drivers/SoC/GPIO/Source/gpio.c
    ${ROOT}/Drivers/SoC/UART/Source/uart.c
    ${PROJECT_DIR}/Src/main.c
)

set_source_files_properties(${DEVICE_DIR}/Source/GCC/startup_S300.S PROPERTIES LANGUAGE ASM)

add_executable(${TARGET} ${SRCS})

# Link board support library
target_link_libraries(${TARGET} s300_board)

# Linker script
set_target_properties(${TARGET} PROPERTIES
    LINK_FLAGS "-T${LD_SCRIPT}"
)

# 生成二进制文件和大小信息
add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bin
    COMMAND arm-none-eabi-objcopy -O ihex $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.hex
    COMMAND arm-none-eabi-size $<TARGET_FILE:${TARGET}>
    COMMENT "Generating binary, hex and size info for Hello World demo"
)

# Debug target: 启动 GDB 调试
set(GDB_PORT 3333 CACHE STRING "GDB server port")
add_custom_target(dbg_hello_world
    COMMENT "Launching GDB for ${TARGET} (GDB_PORT=${GDB_PORT})"
    COMMAND arm-none-eabi-gdb -q
    -ex "file $<TARGET_FILE:${TARGET}>"
    -ex "target extended-remote :${GDB_PORT}"
    -x ${ROOT}/gdbinit.gdb
    DEPENDS ${TARGET}
    USES_TERMINAL
)
