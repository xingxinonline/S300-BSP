cmake_minimum_required(VERSION 3.16)

# Set the toolchain for ARM Cortex-M4
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Specify the cross compiler
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(CMAKE_C_COMPILER arm-none-eabi-gcc.exe)
    set(CMAKE_CXX_COMPILER arm-none-eabi-g++.exe)
    set(CMAKE_ASM_COMPILER arm-none-eabi-gcc.exe)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_C_COMPILER arm-none-eabi-gcc)
    set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
    set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
else()
    message(FATAL_ERROR "Unsupported host system: ${CMAKE_HOST_SYSTEM_NAME}")
endif()

# Since it's bare-metal, don't try to link executables during compiler tests
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Set the project name
project(S300_BSP)

# =============================================================================
# Board Selection (Multi-board Support)
# =============================================================================
# Usage: cmake -DBOARD=<board_name> ...
# Available boards: generic_evb, ...  (add new boards under Boards/)
set(BOARD "generic_evb" CACHE STRING "Target board name (e.g., generic_evb)")
message(STATUS "Building for board: ${BOARD}")

# Set compiler flags
# Debug level: -g1 (minimal, fast loading) | -g2 (standard) | -g3 (full, slow loading)
# Use -g1 for faster GDB symbol loading, -g3 for detailed macro debugging
set(DEBUG_LEVEL "-g1" CACHE STRING "Debug information level (-g1=minimal/fast, -g2=standard, -g3=full/slow)")
set(CMAKE_C_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os -ffunction-sections -fdata-sections -fno-builtin -Wall -Wextra ${DEBUG_LEVEL} -gdwarf-2")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS}")

# Enable ASM language
enable_language(ASM)

# Set linker flags
set(CMAKE_EXE_LINKER_FLAGS "-nostartfiles -Wl,--gc-sections -specs=nosys.specs")

# Add subdirectories (order matters: Boards must be added before Drivers/Projects)
add_subdirectory(CMSIS)
add_subdirectory(Boards)
add_subdirectory(Drivers)
add_subdirectory(Projects)

# Add debug target (equivalent to make dbg)
# GDB port can be customized; default OpenOCD/J-Link is :3333
set(GDB_PORT 3333 CACHE STRING "GDB server port")

if(TARGET s300_rbl_minimal)
    add_custom_target(dbg_rbl
        COMMENT "Launching GDB for s300_rbl_minimal (RBL - SRAM: 0x20000000) (GDB_PORT=${GDB_PORT})"
        COMMAND arm-none-eabi-gdb -q
        -ex "file $<TARGET_FILE:s300_rbl_minimal>"
        -ex "target extended-remote :${GDB_PORT}"
        -x ${CMAKE_SOURCE_DIR}/gdbinit.gdb
        DEPENDS s300_rbl_minimal
        USES_TERMINAL
    )
endif()

if(TARGET s300_sbl_minimal)
    add_custom_target(dbg_sbl
        COMMENT "Launching GDB for s300_sbl_minimal (SBL - Flash: 0x08010000) (GDB_PORT=${GDB_PORT})"
        COMMAND arm-none-eabi-gdb -q
        -ex "file $<TARGET_FILE:s300_sbl_minimal>"
        -ex "target extended-remote :${GDB_PORT}"
        -x ${CMAKE_SOURCE_DIR}/gdbinit.flash.gdb
        DEPENDS s300_sbl_minimal
        USES_TERMINAL
    )
endif()

if(TARGET s300_rbl_minimal)
    add_custom_target(dbg DEPENDS dbg_rbl)
elseif(TARGET s300_sbl_minimal)
    add_custom_target(dbg DEPENDS dbg_sbl)
elseif(TARGET s300_display_demo)
    add_custom_target(dbg DEPENDS dbg_display)
elseif(TARGET s300_uart3_echo)
    add_custom_target(dbg DEPENDS dbg_uart3_echo)
elseif(TARGET s300_dma_uart3_tx)
    add_custom_target(dbg DEPENDS dbg_dma_uart3_tx)
elseif(TARGET s300_dma_uart3_rx)
    add_custom_target(dbg DEPENDS dbg_dma_uart3_rx)
elseif(TARGET s300_dma_ram2ram)
    add_custom_target(dbg DEPENDS dbg_dma_ram2ram)
elseif(TARGET s300_dma_interrupt)
    add_custom_target(dbg DEPENDS dbg_dma_interrupt)
elseif(TARGET s300_freertos_shell)
    add_custom_target(dbg DEPENDS dbg_freertos_shell)
endif()

# Set the default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()