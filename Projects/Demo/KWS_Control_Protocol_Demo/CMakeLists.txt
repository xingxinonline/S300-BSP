cmake_minimum_required(VERSION 3.16)
project(KWS_Control_Protocol_Demo)

set(TARGET s300_kws_control_protocol_demo)
set(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ROOT ${PROJECT_DIR}/../../..)
set(AUDIO_SHARED_DIR ${ROOT}/Projects/Demo/Audio_KWS_Demo/Src)
set(DSP_BIN_DIR ${PROJECT_DIR}/dsp_bin)
set(KWS_CTRL_GDB_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.kws-control-protocol.gdb)

if(BOARD STREQUAL "app_board" OR BOARD STREQUAL "gimbal_master")
    set(AUDIO_CODEC_DRIVERS es7210 es8311)
    message(STATUS "KWS_Control_Protocol_Demo: Using ES7210+ES8311 codec for ${BOARD}")
else()
    set(AUDIO_CODEC_DRIVERS wm8978)
    message(STATUS "KWS_Control_Protocol_Demo: Using WM8978 codec for ${BOARD}")
endif()

s300_resolve_drivers(
    DRIVERS rcc gpio uart dma i2s i2c_soft mailbox ${AUDIO_CODEC_DRIVERS}
    OUT_SRCS DRIVER_SRCS
    OUT_INCS DRIVER_INCS
)

include_directories(
    ${S300_DEVICE_DIR}/Include
    ${S300_CORE_INC}
    ${DRIVER_INCS}
    ${AUDIO_SHARED_DIR}
    ${ROOT}/Algorithm_Models/protocol
    ${PROJECT_DIR}/Src
)

set(SRCS
    ${S300_STARTUP_FILE}
    ${S300_SYSTEM_FILE}
    ${DRIVER_SRCS}
    ${PROJECT_DIR}/Src/main.c
    ${PROJECT_DIR}/Src/kws_control_stream.c
    ${AUDIO_SHARED_DIR}/audio_app.c
    ${AUDIO_SHARED_DIR}/audio_codec.c
)

set_source_files_properties(${S300_STARTUP_FILE} PROPERTIES LANGUAGE ASM)

add_executable(${TARGET} ${SRCS})
target_link_libraries(${TARGET} s300_board)
set_target_properties(${TARGET} PROPERTIES LINK_FLAGS "-T${S300_DEFAULT_LD}")

if(BOARD STREQUAL "app_board" OR BOARD STREQUAL "gimbal_master")
    target_compile_definitions(${TARGET} PRIVATE
        BOARD_AUDIO_CODEC_ES7210=1
        BOARD_AUDIO_CODEC_ES8311=1
        BOARD_AUDIO_ENABLE=1
    )
else()
    target_compile_definitions(${TARGET} PRIVATE
        BOARD_AUDIO_CODEC_WM8978=1
        BOARD_AUDIO_ENABLE=1
    )
endif()

add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bin
    COMMAND arm-none-eabi-objcopy -O ihex $<TARGET_FILE:${TARGET}> ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.hex
    COMMAND arm-none-eabi-objdump -d -S -C -M force-thumb,reg-names-std -j .text $<TARGET_FILE:${TARGET}> > ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.dis
    COMMAND arm-none-eabi-size $<TARGET_FILE:${TARGET}>
    COMMAND ${CMAKE_COMMAND}
        -DDSP_BIN_DIR=${DSP_BIN_DIR}
        -DTEMPLATE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/gdbinit.kws_control_protocol.gdb.in
        -DOUTPUT_FILE=${KWS_CTRL_GDB_SCRIPT}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_kws_control_protocol_gdb.cmake
    COMMENT "Generating artifacts and refreshing KWS control protocol GDB script"
)

set(GDB_PORT 3333 CACHE STRING "GDB server port")

add_custom_target(dbg_kws_control_protocol_demo
    COMMENT "Launching GDB for ${TARGET} (GDB_PORT=${GDB_PORT})"
    COMMAND arm-none-eabi-gdb -q
    -ex "file $<TARGET_FILE:${TARGET}>"
    -ex "target extended-remote :${GDB_PORT}"
    -x ${KWS_CTRL_GDB_SCRIPT}
    DEPENDS ${TARGET}
    USES_TERMINAL
)

add_s300_image(${TARGET}
    OUTPUT s300_kws_control_protocol_demo.bin
    DSP_DIR ${DSP_BIN_DIR}
)

message(STATUS "KWS_Control_Protocol_Demo: DSP bin dir = ${DSP_BIN_DIR}")