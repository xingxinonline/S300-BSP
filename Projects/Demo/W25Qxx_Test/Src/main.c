#include "s300.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "rcc.h"
#include "gpio.h"
#include "uart.h"
#include "board.h"
#include "qspi_cadence.h"
#include "w25qxx.h"

#define SYSTICK_BASE 0xE000E010
typedef struct {
    volatile uint32_t CTRL;
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
    volatile uint32_t CALIB;
} systick_t;
#define SYSTICK ((systick_t *)SYSTICK_BASE)

static uint32_t systick_reload;

static void systick_init(uint32_t reload) {
    systick_reload = reload;
    SYSTICK->LOAD = reload - 1;
    SYSTICK->VAL = 0;
    SYSTICK->CTRL = 0x5; // enable, no interrupt, processor clock
}

static uint32_t systick_get_ticks(void) {
    return SYSTICK->VAL;
}

static uint32_t systick_elapsed_us(uint32_t start_ticks, uint32_t end_ticks) {
    uint32_t ticks;
    if (start_ticks >= end_ticks) {
        ticks = start_ticks - end_ticks;
    } else {
        ticks = start_ticks + (systick_reload) - end_ticks;
    }
    return ticks;
}

static void print_hex(const char *tag, const uint8_t *buf, unsigned len)
{
    printf("%s", tag);
    for (unsigned i = 0; i < len; ++i)
        printf(" %02X", buf[i]);
    printf("\r\n");
}
static void print_status_all(const char *tag)
{
    uint8_t s1 = 0, s2 = 0, s3 = 0;
    if (qspi_read_status(&s1, &s2, &s3) == 0) {
        unsigned busy = (s1 & 0x01u) ? 1u : 0u;
        unsigned wel  = (s1 & 0x02u) ? 1u : 0u;
        unsigned qe   = (s2 & 0x02u) ? 1u : 0u;
        printf("%s SR1=0x%02X (BUSY=%u WEL=%u) SR2=0x%02X (QE=%u) SR3=0x%02X\r\n",
               tag ? tag : "[DEMO]",
               s1, busy, wel, s2, qe, s3);
    } else {
        printf("%s read status failed\r\n", tag ? tag : "[DEMO]");
    }
}

static int xip_exec_test(uint32_t code_addr)
{
    uint32_t start_ticks = systick_get_ticks();
    /* Thumb 小函数：movs r0,#0x5A; bx lr */
    const uint8_t code[] = { 0x5A, 0x20, 0x70, 0x47 };

    printf("[DEMO] XIP exec test: prepare code at 0x%08lX...\r\n", (unsigned long)code_addr);
    /* 擦除并写入 4 字节 */
    if (w25qxx_erase_4k(code_addr & ~0xFFFu) != 0) {
        printf("[DEMO] XIP exec: erase failed\r\n");
        return -1;
    }
    if (w25qxx_write_page(code_addr, code, sizeof(code)) != 0) {
        printf("[DEMO] XIP exec: program failed\r\n");
        return -2;
    }
    uint8_t rb[4] = {0};
    if (w25qxx_read(code_addr, rb, sizeof(rb)) != 0) {
        printf("[DEMO] XIP exec: readback failed\r\n");
        return -3;
    }
    if (memcmp(code, rb, sizeof(code)) != 0) {
        print_hex("[DEMO] XIP exec: verify W:", code, sizeof(code));
        print_hex("[DEMO] XIP exec: verify R:", rb, sizeof(rb));
        return -4;
    }

    /* 进入 XIP 并通过 AHB 窗口执行 */
    printf("[DEMO] Enter XIP 1-4-4 for exec...\r\n");
    if (w25qxx_enter_xip_144(4u, 0x20u) != 0) {
        printf("[DEMO] XIP exec: enter XIP failed\r\n");
        return -5;
    }
    {
        volatile const uint8_t *xip = (volatile const uint8_t *)g_qspi.ahb;
        uint8_t peek[4];
        for (unsigned i = 0; i < sizeof(peek); ++i) peek[i] = xip[code_addr + i];
        print_hex("[DEMO] XIP exec: peek:", peek, sizeof(peek));

        /* 执行：Thumb 函数指针需将地址 bit0 置 1 */
        typedef int (*xip_fn_t)(void);
        uintptr_t fn_addr = (uintptr_t)(xip + code_addr);
        xip_fn_t fn = (xip_fn_t)(fn_addr | 1u);
        uint32_t exec_start = systick_get_ticks();
        int ret = fn();
        uint32_t exec_end = systick_get_ticks();
        uint32_t exec_us = systick_elapsed_us(exec_start, exec_end);
        printf("[DEMO] XIP exec: fn() returned 0x%02X in %lu us\r\n", (unsigned)ret, (unsigned long)exec_us);
        if (ret != 0x5A) {
            printf("[DEMO] XIP exec: unexpected return value\r\n");
            w25qxx_exit_xip();
            return -6;
        }
    }
    w25qxx_exit_xip();
    printf("[DEMO] XIP exec: done and exited XIP\r\n");

    /* 退出后快速校验：读状态寄存器确保不在忙/连续读状态 */
    uint8_t sr1 = 0;
        uint8_t sr2 = 0, sr3 = 0;
        (void)qspi_read_status(&sr1, &sr2, &sr3);
    printf("[DEMO] Post-XIP SR1=0x%02X\r\n", sr1);
        /* 如果看起来保护位/SRP 被置位，尝试解锁一次 */
        if ((sr1 & 0xBCu) || (sr2 & 0x78u)) {
            printf("[DEMO] Detected protect bits set after XIP, try unlock...\r\n");
            (void)qspi_unlock_all();
            (void)qspi_read_status(&sr1, &sr2, &sr3);
            printf("[DEMO] After unlock SR1=0x%02X, SR2=0x%02X, SR3=0x%02X\r\n", sr1, sr2, sr3);
        }
    uint32_t end_ticks = systick_get_ticks();
    uint32_t total_us = systick_elapsed_us(start_ticks, end_ticks);
    printf("[DEMO] XIP exec test total time: %lu us\r\n", (unsigned long)total_us);
    return 0;
}

static int flash_test_rw(uint32_t test_addr)
{
    const unsigned PAGE = 256;
    uint8_t w[PAGE];
    uint8_t r[PAGE];

    // pattern
    for (unsigned i = 0; i < PAGE; ++i)
        w[i] = (uint8_t)(i ^ 0xA5 ^ (test_addr >> 8));

    printf("[DEMO] Erase 4KB at 0x%08lX...\r\n", (unsigned long)(test_addr & ~0xFFFu));
    uint32_t erase_start = systick_get_ticks();
    if (w25qxx_erase_4k(test_addr & ~0xFFFu) != 0) {
        printf("[DEMO] Erase failed\r\n");
        return -1;
    }
    uint32_t erase_end = systick_get_ticks();
    uint32_t erase_us = systick_elapsed_us(erase_start, erase_end);
    printf("[DEMO] Erase time: %lu us\r\n", (unsigned long)erase_us);
    printf("[DEMO] Erase speed: %.2f KB/s\r\n", (4096.0f / erase_us) * 1000.0f / 1024.0f);

    printf("[DEMO] Page program 256B at 0x%08lX...\r\n", (unsigned long)test_addr);
    uint32_t prog_start = systick_get_ticks();
    if (w25qxx_write_page(test_addr, w, PAGE) != 0) {
        printf("[DEMO] Program failed\r\n");
        return -2;
    }
    uint32_t prog_end = systick_get_ticks();
    uint32_t prog_us = systick_elapsed_us(prog_start, prog_end);
    printf("[DEMO] Program time: %lu us\r\n", (unsigned long)prog_us);
    printf("[DEMO] Program speed: %.2f KB/s\r\n", (256.0f / prog_us) * 1000.0f / 1024.0f);

    memset(r, 0, sizeof(r));
    uint32_t read_start = systick_get_ticks();
    if (w25qxx_read(test_addr, r, PAGE) != 0) {
        printf("[DEMO] Read failed\r\n");
        return -3;
    }
    uint32_t read_end = systick_get_ticks();
    uint32_t read_us = systick_elapsed_us(read_start, read_end);
    printf("[DEMO] Read time: %lu us\r\n", (unsigned long)read_us);
    printf("[DEMO] Read speed: %.2f KB/s\r\n", (256.0f / read_us) * 1000.0f / 1024.0f);

    if (memcmp(w, r, PAGE) != 0) {
        printf("[DEMO] Verify failed\r\n");
        print_hex("W:", w, 32);
        print_hex("R:", r, 32);
        return -4;
    }

    printf("[DEMO] Verify OK\r\n");
    return 0;
}

int main(void)
{
    // 基本板级初始化（时钟、UART3 115200、printf）
    board_init();

    // QSPI 初始化：ref=AHB时钟，SCLK设为较安全值（例如AHB/6）
    uint32_t ahb_hz = rcc_get_clock(RCC_CLOCK_AHB);
    uint32_t sclk_hz = ahb_hz / 2u; // 高速测试：~96MHz（根据芯片时序需要配套更长dummy）

    // 初始化 systick 为 1us 精度
    systick_init(ahb_hz / 1000000u);

    printf("\r\n[DEMO] W25Qxx test start. AHB=%lu Hz, QSPI SCLK=%lu Hz\r\n",
           (unsigned long)ahb_hz, (unsigned long)sclk_hz);

    qspi_set_verbose(false);
    qspi_cadence_init(ahb_hz, sclk_hz);

    // 读取并配置Flash
    w25qxx_info_t info;
    /* 对于 W25Q128（16MiB）不需要 4B 地址模式，保持 3B 更稳妥；
       若后续换成 >16MiB 的器件（如 W25Q256），再开启 4B。 */
    if (w25qxx_init(&info, true, false) != 0) {
        printf("[DEMO] w25qxx_init failed\r\n");
        return -1;
    }

    /* 解锁所有保护区，避免写入被保护位拒绝 */
    (void)qspi_unlock_all();

    printf("[DEMO] JEDEC ID: manuf=0x%02X, type=0x%02X, cap=0x%02X, size=%lu bytes\r\n",
           info.manuf_id, info.memory_type, info.capacity, (unsigned long)info.size_bytes);
    printf("[DEMO] QE=%d, 4B=%d\r\n", info.quad_enabled ? 1 : 0, info.addr4b ? 1 : 0);

    print_status_all("[DEMO] Status(before tests):");

    // 选择一个安全的测试地址，避开可能的Boot/RBL区域
    uint32_t test_addr = 0x00000000u; // 1MB 偏移

    // 基础读写擦测试（STIG 1-1-1）
    int rc = flash_test_rw(test_addr);
    if (rc != 0) {
        printf("[DEMO] R/W test failed (%d)\r\n", rc);
        return rc;
    }

    // 可选：XIP 1-4-4 进入/演示/退出（注意：仅在器件支持QE且建议在>16MiB时使用4B地址）
    printf("[DEMO] Enter XIP 1-4-4...\r\n");
    /* 对 W25Q128：3B + dummy=4 + mode_bits=0x20（与RBL一致）更稳妥，尤其在>=80MHz时 */
    if (w25qxx_enter_xip_144(4u, 0x20u) == 0) {
        volatile const uint8_t *xip = (volatile const uint8_t *)g_qspi.ahb;
        uint8_t peek[16];
        uint32_t xip_read_start = systick_get_ticks();
        for (unsigned i = 0; i < sizeof(peek); ++i) {
            peek[i] = xip[test_addr + i];
        }
        uint32_t xip_read_end = systick_get_ticks();
        uint32_t xip_read_us = systick_elapsed_us(xip_read_start, xip_read_end);
        print_hex("[DEMO] XIP peek:", peek, sizeof(peek));
        printf("[DEMO] XIP read 16B time: %lu us, speed: %.2f KB/s\r\n", (unsigned long)xip_read_us, (16.0f / xip_read_us) * 1000.0f / 1024.0f);
        w25qxx_exit_xip();
        printf("[DEMO] Exit XIP.\r\n");
    } else {
        printf("[DEMO] Enter XIP failed, skip XIP demo.\r\n");
    }

    /* XIP 执行测试：在另一扇区写入 Thumb 函数并执行 */
    uint32_t code_addr = 0x00102000u; /* 与数据测试分离的安全扇区 */
    (void)xip_exec_test(code_addr);

    /* 多次进入/退出 XIP 回归测试，验证 XIP 前后 STIG 功能正常 */
    for (unsigned i = 0; i < 5u; ++i) {
        printf("[DEMO] XIP cycle %u: enter...\r\n", i);
        uint32_t cycle_start = systick_get_ticks();
        if (w25qxx_enter_xip_144(4u, 0x20u) != 0) {
            printf("[DEMO] XIP cycle %u: enter failed\r\n", i);
            break;
        }
        volatile const uint8_t *xip = (volatile const uint8_t *)g_qspi.ahb;
        uint8_t p2[8];
        for (unsigned k = 0; k < sizeof(p2); ++k) p2[k] = xip[test_addr + k];
        print_hex("[DEMO] XIP cycle peek:", p2, sizeof(p2));
        w25qxx_exit_xip();
        uint32_t cycle_end = systick_get_ticks();
        uint32_t cycle_us = systick_elapsed_us(cycle_start, cycle_end);
        printf("[DEMO] XIP cycle %u time: %lu us\r\n", i, (unsigned long)cycle_us);
        /* 退出后做一次 STIG 读验证 */
        uint8_t chk[8] = {0};
        (void)w25qxx_read(test_addr, chk, sizeof(chk));
        print_hex("[DEMO] STIG peek after exit:", chk, sizeof(chk));
    }

    printf("[DEMO] W25Qxx test DONE.\r\n");
    while (1) {
        __WFI();
    }
}
