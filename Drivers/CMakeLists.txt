# Drivers CMakeLists.txt
# Build SoC driver libraries (minimal subset for video/display)

set(DRIVERS_ROOT ${CMAKE_CURRENT_LIST_DIR})

add_library(s300_video STATIC
    ${DRIVERS_ROOT}/SoC/MM/Source/video.c
)

target_include_directories(s300_video PUBLIC
    ${PROJECT_SOURCE_DIR}/CMSIS/Device/PiMCHIP/S300/Include
    ${PROJECT_SOURCE_DIR}/CMSIS/Core/Include
    ${DRIVERS_ROOT}/SoC/MM/Include
    ${DRIVERS_ROOT}/SoC/GPIO/Include
    ${DRIVERS_ROOT}/SoC/RCC/Include

    # Board includes (only headers, not sources)
    ${S300_BOARD_DIR}
    ${S300_BOARD_COMMON_DIR}
)

# Mailbox driver as a reusable static library for demos/tests
add_library(s300_mailbox STATIC
    ${DRIVERS_ROOT}/SoC/MAILBOX/Source/mailbox.c
)

target_include_directories(s300_mailbox PUBLIC
    ${PROJECT_SOURCE_DIR}/CMSIS/Device/PiMCHIP/S300/Include
    ${PROJECT_SOURCE_DIR}/CMSIS/Core/Include
    ${DRIVERS_ROOT}/SoC/MAILBOX/Include
)

# PSRAM driver
add_library(s300_psram STATIC
    ${DRIVERS_ROOT}/SoC/PSRAM/Source/psram.c
)

target_include_directories(s300_psram PUBLIC
    ${PROJECT_SOURCE_DIR}/CMSIS/Device/PiMCHIP/S300/Include
    ${PROJECT_SOURCE_DIR}/CMSIS/Core/Include
    ${DRIVERS_ROOT}/SoC/PSRAM/Include
)
