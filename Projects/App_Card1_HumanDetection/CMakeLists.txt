cmake_minimum_required(VERSION 3.16)
project(App_Card1_HumanDetection)

s300_add_executable(
    TARGET s300_card1_human_detection
    SOURCES
        Src/main.c
        Src/card1_detection.c
        Src/card1_i2c_slave.c
    DRIVERS rcc gpio uart mm mailbox psram i2c
    INCLUDES . Inc ${CMAKE_SOURCE_DIR}/Algorithm_Models/protocol
    DBG_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.human-detection.gdb
    NO_IMAGE
)

target_compile_definitions(s300_card1_human_detection PRIVATE
    BOARD_MM_ENABLE=1
)

target_compile_options(s300_card1_human_detection PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include card1_video_compat.h>
)

set(GDB_PORT 3333 CACHE STRING "GDB server port")

cmake_path(ABSOLUTE_PATH CMAKE_SOURCE_DIR NORMALIZE OUTPUT_VARIABLE ROOT_ABS)
set(HUMAN_MODEL_DIR ${ROOT_ABS}/Algorithm_Models/Human_Detection)

function(generate_gdb_restore_cmd IMAGE_FILE ADDR OUTPUT_VAR)
    if(EXISTS "${IMAGE_FILE}")
        file(SIZE "${IMAGE_FILE}" FILE_SIZE)
        cmake_path(NORMAL_PATH IMAGE_FILE OUTPUT_VARIABLE IMAGE_FILE_CLEAN)

        if(FILE_SIZE GREATER 0)
            set(${OUTPUT_VAR} "restore ${IMAGE_FILE_CLEAN} binary ${ADDR}" PARENT_SCOPE)
        else()
            set(${OUTPUT_VAR} "# ${IMAGE_FILE_CLEAN} is empty, skipping" PARENT_SCOPE)
        endif()
    else()
        set(${OUTPUT_VAR} "# ${IMAGE_FILE} not found, skipping" PARENT_SCOPE)
    endif()
endfunction()

set(MODEL_NAME "Human_Detection")
set(MODEL_KEBAB "human-detection")
set(MODEL_DIR ${HUMAN_MODEL_DIR})
set(PSRAM_BREAKPOINT_FUNC "main")

generate_gdb_restore_cmd("${HUMAN_MODEL_DIR}/model_dtcm_boot.bin" "0x44800000" GDB_RESTORE_DTCM)
generate_gdb_restore_cmd("${HUMAN_MODEL_DIR}/model_ptcm_boot.bin" "0x44A00000" GDB_RESTORE_PTCM)
generate_gdb_restore_cmd("${HUMAN_MODEL_DIR}/model_sram0_boot.bin" "0x44000000" GDB_RESTORE_SRAM0)
generate_gdb_restore_cmd("${HUMAN_MODEL_DIR}/model_sram1_boot.bin" "0x44040000" HUMAN_RESTORE_SRAM1)
set(GDB_RESTORE_PSRAM "${HUMAN_RESTORE_SRAM1}")

configure_file(
    ${CMAKE_SOURCE_DIR}/gdbinit.model.gdb.in
    ${CMAKE_CURRENT_BINARY_DIR}/gdbinit.${MODEL_KEBAB}.gdb
    @ONLY
)

add_s300_image(s300_card1_human_detection
    OUTPUT s300_card1_human_detection.bin
    DSP_DIR ${HUMAN_MODEL_DIR}
)

add_custom_target(dbg_card1
    DEPENDS dbg_card1_human_detection
)
