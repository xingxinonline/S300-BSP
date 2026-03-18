function(dsp_dir_is_usable dir output_var)
    if(EXISTS "${dir}/model_dtcm_boot.bin" AND EXISTS "${dir}/model_ptcm_boot.bin")
        set(${output_var} TRUE PARENT_SCOPE)
    else()
        set(${output_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

if(NOT DEFINED TOOLS_DIR)
    message(FATAL_ERROR "TOOLS_DIR is required")
endif()

if(NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE is required")
endif()

if(NOT DEFINED M4_BIN_FILE)
    message(FATAL_ERROR "M4_BIN_FILE is required")
endif()

set(RESOLVED_DSP_DIR "")

if(DEFINED DSP_DIR AND NOT "${DSP_DIR}" STREQUAL "")
    set(RESOLVED_DSP_DIR "${DSP_DIR}")
endif()

if(DEFINED DSP_DIR_FALLBACK AND NOT "${DSP_DIR_FALLBACK}" STREQUAL "")
    dsp_dir_is_usable("${DSP_DIR}" PRIMARY_DSP_DIR_OK)
    if(PRIMARY_DSP_DIR_OK)
        set(RESOLVED_DSP_DIR "${DSP_DIR}")
    else()
        set(RESOLVED_DSP_DIR "${DSP_DIR_FALLBACK}")
    endif()
endif()

set(CMD_ARGS generate -o "${OUTPUT_FILE}" --m4 "${M4_BIN_FILE}")

if(NOT "${RESOLVED_DSP_DIR}" STREQUAL "")
    list(APPEND CMD_ARGS --dsp "${RESOLVED_DSP_DIR}")
    message(STATUS "generate_s300_image: DSP dir = ${RESOLVED_DSP_DIR}")
endif()

if(DEFINED M0_BIN AND NOT "${M0_BIN}" STREQUAL "")
    list(APPEND CMD_ARGS --m0 "${M0_BIN}")
endif()

if(DEFINED DSP_PRO AND NOT "${DSP_PRO}" STREQUAL "")
    list(APPEND CMD_ARGS --dsp-pro "${DSP_PRO}")
endif()

execute_process(
    COMMAND python s300_image.py ${CMD_ARGS}
    WORKING_DIRECTORY "${TOOLS_DIR}"
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE GENERATE_IMAGE_RESULT
)

if(NOT GENERATE_IMAGE_RESULT EQUAL 0)
    message(FATAL_ERROR "generate_s300_image failed with exit code ${GENERATE_IMAGE_RESULT}")
endif()