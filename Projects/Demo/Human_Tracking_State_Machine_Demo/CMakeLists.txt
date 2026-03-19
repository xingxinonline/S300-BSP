cmake_minimum_required(VERSION 3.16)
project(Human_Tracking_State_Machine_Demo)

set(TARGET s300_human_tracking_state_machine_demo)
set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ROOT ${PROJECT_DIR}/../../..)
set(SHARED_DETECTION_DIR ${ROOT}/Projects/Demo/Algorithm_Detection_Demo)
set(DSP_BIN_DIR ${PROJECT_DIR}/model_bin)
set(HT_CTRL_GDB_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.human_tracking_state_machine.gdb)

s300_resolve_drivers(
    DRIVERS uart dma mm mailbox psram
    OUT_SRCS DRIVER_SRCS
    OUT_INCS DRIVER_INCS
)

include_directories(
    ${S300_DEVICE_DIR}/Include
    ${S300_CORE_INC}
    ${DRIVER_INCS}
    ${PROJECT_DIR}/Inc
    ${SHARED_DETECTION_DIR}/Inc
    ${ROOT}/Algorithm_Models/protocol
)

add_compile_definitions(BOARD_MM_ENABLE=1)

set(SRCS
    ${S300_STARTUP_FILE}
    ${S300_SYSTEM_FILE}
    ${DRIVER_SRCS}
    ${PROJECT_DIR}/Src/main.c
    ${PROJECT_DIR}/Src/human_tracking_app.c
    ${PROJECT_DIR}/Src/human_tracking_overlay.c
    ${SHARED_DETECTION_DIR}/Src/camera_ov5640.c
)

set_source_files_properties(${S300_STARTUP_FILE} PROPERTIES LANGUAGE ASM)

add_executable(${TARGET} ${SRCS})
target_link_libraries(${TARGET} s300_board m)
set_target_properties(${TARGET} PROPERTIES LINK_FLAGS "-T${S300_DEFAULT_LD}")

add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bin
    COMMAND arm-none-eabi-objcopy -O ihex $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.hex
    COMMAND arm-none-eabi-objdump -d -S -C -M force-thumb,reg-names-std -j .text $<TARGET_FILE:${TARGET}> > ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.dis
    COMMAND arm-none-eabi-size $<TARGET_FILE:${TARGET}>
    COMMAND ${CMAKE_COMMAND}
        -DDSP_BIN_DIR=${DSP_BIN_DIR}
        -DTEMPLATE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/gdbinit.human_tracking_state_machine.gdb.in
        -DOUTPUT_FILE=${HT_CTRL_GDB_SCRIPT}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_human_tracking_state_machine_gdb.cmake
    COMMENT "Generating artifacts and refreshing human tracking state machine GDB script"
)

set(GDB_PORT 3333 CACHE STRING "GDB server port")

add_custom_target(dbg_human_tracking_state_machine_demo
    COMMENT "Launching GDB for ${TARGET} (GDB_PORT=${GDB_PORT})"
    COMMAND arm-none-eabi-gdb -q
    -ex "file $<TARGET_FILE:${TARGET}>"
    -ex "target extended-remote :${GDB_PORT}"
    -x ${HT_CTRL_GDB_SCRIPT}
    DEPENDS ${TARGET}
    USES_TERMINAL
)

add_s300_image(${TARGET}
    OUTPUT s300_human_tracking_state_machine_demo.bin
    DSP_DIR ${DSP_BIN_DIR}
)

message(STATUS "Human_Tracking_State_Machine_Demo: model bin dir = ${DSP_BIN_DIR}")