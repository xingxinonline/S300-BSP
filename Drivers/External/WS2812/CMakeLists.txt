# WS2812 - RGB LED 灯带驱动
# 支持 WS2812/WS2812B/SK6812 等兼容型号

# 防止重复定义 (多个 Demo 使用时)
if(TARGET ws2812)
    return()
endif()

add_library(ws2812 STATIC
    Source/ws2812.c
)

target_include_directories(ws2812 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

# 添加 SoC 驱动的 include 路径
target_include_directories(ws2812 PRIVATE
    ${CMAKE_SOURCE_DIR}/Drivers/SoC/GPIO/Include
    ${CMAKE_SOURCE_DIR}/Drivers/SoC/RCC/Include
    ${CMAKE_SOURCE_DIR}/CMSIS/Core/Include
    ${CMAKE_SOURCE_DIR}/CMSIS/Device/PiMCHIP/Include
    ${CMAKE_SOURCE_DIR}/CMSIS/Device/PiMCHIP/S300/Include
)
