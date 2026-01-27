# BusServo - 总线舵机驱动 (Hiwonder 协议)
# 支持半双工 UART 通信的总线舵机控制

add_library(bus_servo STATIC
    Source/bus_servo.c
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
