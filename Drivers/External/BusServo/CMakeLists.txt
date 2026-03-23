# BusServo - 总线舵机驱动 (Hiwonder 协议)
# 支持半双工 UART 通信的总线舵机控制

# 防止重复定义 (多个 Demo 使用时)
if(TARGET bus_servo)
    return()
endif()

set(BUS_SERVO_DEBUG_LOG "OFF" CACHE STRING "Enable low-level BusServo TX/RX debug log: ON or OFF")

add_library(bus_servo STATIC
    Source/bus_servo.c
)

target_compile_definitions(bus_servo PUBLIC
    $<$<STREQUAL:${BUS_SERVO_DEBUG_LOG},ON>:BUS_SERVO_DEBUG=1>
)

target_include_directories(bus_servo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

# 添加 SoC 驱动的 include 路径
target_include_directories(bus_servo PRIVATE
    ${CMAKE_SOURCE_DIR}/Drivers/SoC/GPIO/Include
    ${CMAKE_SOURCE_DIR}/Drivers/SoC/UART/Include
    ${CMAKE_SOURCE_DIR}/Drivers/SoC/RCC/Include
    ${CMAKE_SOURCE_DIR}/CMSIS/Core/Include
    ${CMAKE_SOURCE_DIR}/CMSIS/Device/PiMCHIP/Include
    ${CMAKE_SOURCE_DIR}/CMSIS/Device/PiMCHIP/S300/Include
)
