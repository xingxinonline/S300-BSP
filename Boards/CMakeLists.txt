# =============================================================================
# Boards/CMakeLists.txt - Board Support Package Configuration
# =============================================================================
# 板级支持包管理，提供统一的板型配置接口
#
# 使用方式:
# cmake -DBOARD=generic_evb ...   (默认)
# cmake -DBOARD=custom_board ...  (自定义板型)
#
# 各项目通过 target_link_libraries(${TARGET} s300_board) 来引用板级配置
# =============================================================================

# BOARD 变量由顶层 CMakeLists.txt 定义，这里做验证
if(NOT DEFINED BOARD)
    message(FATAL_ERROR "BOARD variable is not defined. Please set it in the top-level CMakeLists.txt")
endif()

set(BOARD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${BOARD})
set(BOARD_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Common)

# 验证板型目录存在
if(NOT EXISTS ${BOARD_DIR}/board.h)
    message(FATAL_ERROR "Board '${BOARD}' not found! Expected: ${BOARD_DIR}/board.h")
endif()

message(STATUS "=== Board Configuration ===")
message(STATUS "  BOARD:      ${BOARD}")
message(STATUS "  BOARD_DIR:  ${BOARD_DIR}")

# =============================================================================
# s300_board: 板级配置 INTERFACE 库
# =============================================================================
# INTERFACE 库不产生实际的库文件，仅传递编译配置
# 包含: 头文件路径、源文件、编译定义等
add_library(s300_board INTERFACE)

# 头文件搜索路径
target_include_directories(s300_board INTERFACE
    ${BOARD_DIR}
    ${BOARD_COMMON_DIR}
)

# 板级源文件 (board.c, retarget.c, syscalls.c)
# 注意: 源文件只应被最终可执行目标包含，不应被中间 STATIC 库包含
# 使用 INTERFACE 方式传递源文件列表，链接此库的 EXECUTABLE 会自动包含这些源文件
target_sources(s300_board INTERFACE
    ${BOARD_DIR}/board.c
    ${BOARD_COMMON_DIR}/retarget.c
    ${BOARD_COMMON_DIR}/syscalls.c
)

# 板级源文件依赖 UART 驱动头文件，添加必要的 include 路径
target_include_directories(s300_board INTERFACE
    ${PROJECT_SOURCE_DIR}/Drivers/SoC/UART/Include
    ${PROJECT_SOURCE_DIR}/Drivers/SoC/RCC/Include
    ${PROJECT_SOURCE_DIR}/Drivers/SoC/GPIO/Include
)

# =============================================================================
# 导出板级路径变量供其他模块使用
# =============================================================================
# 这些变量可在子项目中直接使用
set(S300_BOARD_DIR ${BOARD_DIR} CACHE PATH "Board directory path" FORCE)
set(S300_BOARD_COMMON_DIR ${BOARD_COMMON_DIR} CACHE PATH "Board common directory path" FORCE)
set(S300_BOARD_NAME ${BOARD} CACHE STRING "Current board name" FORCE)
