# =============================================================================
# Mailbox_Test Demo - 核间邮箱通信测试
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(Mailbox_Test)

s300_add_executable(
    TARGET s300_mailbox_test
    SOURCES Src/main.c
    DRIVERS rcc gpio uart mailbox
    DBG_NAME mailbox
)
