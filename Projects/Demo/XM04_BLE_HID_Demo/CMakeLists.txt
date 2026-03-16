# =============================================================================
# XM04_BLE_HID_Demo - XM04 蓝牙 HID/AT 调试 Demo
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(XM04_BLE_HID_Demo)

if(NOT BOARD STREQUAL "gimbal_master")
    message(STATUS "XM04_BLE_HID_Demo: skipped for BOARD=${BOARD}, gimbal_master only")
    return()
endif()

set(XM04_AT_PIN "21" CACHE STRING "GPIO pin connected to XM04 AT/PIO11")
set(XM04_UART_BAUDRATE "9600" CACHE STRING "XM04 UART baudrate")
set(XM04_ENABLE_AT_MODE "0" CACHE STRING "Enable XM04 AT command path (0/1)")

s300_add_executable(
    TARGET s300_xm04_ble_hid_demo
    SOURCES
        Src/main.c
        Src/xm04_ble.c
        Src/xm04_ring_buffer.c
    DRIVERS rcc gpio uart
    INCLUDES . Inc
)

target_compile_definitions(s300_xm04_ble_hid_demo PRIVATE
    XM04_AT_PIN=${XM04_AT_PIN}
    XM04_UART_BAUDRATE=${XM04_UART_BAUDRATE}
    XM04_ENABLE_AT_MODE=${XM04_ENABLE_AT_MODE}
)