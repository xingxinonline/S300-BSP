/**
 * @file    version.c
 * @brief   S300 BSP 版本信息实现
 * @note    此文件通过 CMake 特殊处理，确保每次构建都重新编译
 *          依赖 build_timestamp.h，该头文件每次构建都会更新
 */

#include "version.h"
#include "build_timestamp.h"  // 每次构建都会更新，强制重编译本文件
#include <stdio.h>

/* 版本字符串存储 */
const char *s300_version_string = S300_VERSION_STRING;
const char *s300_board_name = S300_BOARD_NAME;
const char *s300_build_timestamp = S300_BUILD_TIMESTAMP;
const char *s300_git_branch = S300_GIT_BRANCH;
const char *s300_git_commit = S300_GIT_COMMIT;

/**
 * @brief 打印完整版本信息
 */
void s300_print_version(void)
{
    printf("-------------------------------------------\n");
    printf("  S300 BSP v%s\n", S300_VERSION_STRING);
    printf("  Board:    %s\n", S300_BOARD_NAME);
    printf("  Built:    %s %s\n", S300_COMPILE_DATE, S300_COMPILE_TIME);
    printf("  Git:      %s@%s\n", S300_GIT_BRANCH, S300_GIT_COMMIT);
    printf("-------------------------------------------\n");
}

/**
 * @brief 获取版本号主版本
 */
int s300_get_version_major(void)
{
    return S300_VERSION_MAJOR;
}

/**
 * @brief 获取版本号次版本
 */
int s300_get_version_minor(void)
{
    return S300_VERSION_MINOR;
}

/**
 * @brief 获取版本号补丁版本
 */
int s300_get_version_patch(void)
{
    return S300_VERSION_PATCH;
}
