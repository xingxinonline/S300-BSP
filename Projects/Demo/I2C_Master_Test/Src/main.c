#include "s300.h"
#include "board.h"
#include "gpio.h"
#include "rcc.h"
#include "i2c.h"
#include "version.h"

#include <stdio.h>
#include <string.h>

/* I2C Master 读写测试 */

#define SLAVE_ADDR          0x03
#define TEST_REG_ADDR       0x55
#define TEST_LEN            10
#define I2C_BUS_HZ          400000u
#define TEST_GAP_MS         500u

static void i2c_gpio_init(void)
{
    gpio_set_function(GPIOA, 0, FUNCTION_3);
    gpio_set_function(GPIOA, 1, FUNCTION_3);
    gpio_set_mode(GPIOA, 0, GPIO_UP);
    gpio_set_mode(GPIOA, 1, GPIO_UP);
}

static void delay_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; ++i)
    {
        for (volatile uint32_t j = 0; j < 20000u; ++j)
        {
            __asm volatile("nop");
        }
    }
}

static void print_buffer(const char *label, const uint8_t *buf, int len)
{
    printf("%s: ", label);
    for (int i = 0; i < len; i++)
    {
        printf("0x%02X ", buf[i]);
    }
    printf("\n");
}

static void print_i2c_stats(emI2C i2c)
{
    i2c_stats_t stats = {0};
    i2c_get_stats(i2c, &stats);

    printf("[I2C%d Stats] TX_OK=%lu(%luB) RX_OK=%lu(%luB) TMO=%lu NACK=%lu ARB=%lu ABRT=%lu RECOVER=%lu LAST_ERR=%ld ABRT_SRC=0x%08lX\n",
           i2c,
           (unsigned long)stats.tx_ok_count,
           (unsigned long)stats.tx_ok_bytes,
           (unsigned long)stats.rx_ok_count,
           (unsigned long)stats.rx_ok_bytes,
           (unsigned long)stats.timeout_count,
           (unsigned long)stats.nack_count,
           (unsigned long)stats.arb_lost_count,
           (unsigned long)stats.abort_count,
           (unsigned long)stats.recover_count,
           (long)stats.last_error,
           (unsigned long)stats.last_abort_source);
}

int main(void)
{
    int ret;
    uint8_t tx_buffer[TEST_LEN];
    uint8_t rx_buffer[TEST_LEN];
    uint8_t rx_burst_buffer[TEST_LEN];
    uint32_t test_count = 0;
    
    board_init();

    printf("\n===========================================\n");
    printf("  S300 I2C Master Demo (Read/Write Test)\n");
    printf("  %s @ %s\n", S300_VERSION_STRING, S300_BOARD_NAME);
    printf("  Build: %s\n", S300_BUILD_TIMESTAMP);
    printf("===========================================\n");
    printf("I2C: I2C1 GPIO0/1, Slave: 0x%02X\n", SLAVE_ADDR);

    rcc_set_cortex_m4_apb1_clock(RCC_CM4_APB1_I2C1, true);
    i2c_gpio_init();

    uint32_t apb_clk = rcc_get_clock(RCC_CLOCK_APB1);
    
    /* SDK风格初始化：主机模式 + 400K + RESTART使能 */
    ret = init_i2c(EM_I2C1, EM_I2C_MASTER | EM_I2C_400K | EM_I2C_RESTART_EN,
                   SLAVE_ADDR, apb_clk, I2C_BUS_HZ);

    i2c_reset_stats(EM_I2C1);
    i2c_set_timeout(I2C_DEFAULT_TIMEOUT);
    
        printf("Master init ok (ret=%d), APB1=%lu Hz, I2C=%lu Hz\n",
            ret,
            (unsigned long)apb_clk,
            (unsigned long)I2C_BUS_HZ);
    i2c_dump_regs(EM_I2C1);
    printf("\nTest sequence: Write -> Read -> BurstRead -> Verify\n\n");

    while (1)
    {
        test_count++;
        printf("--- Test #%lu ---\n", (unsigned long)test_count);
        
        /* 1. 准备写入数据 */
        for (int i = 0; i < TEST_LEN; i++)
        {
            tx_buffer[i] = (uint8_t)(0x10 + test_count + i);  /* 每次不同的数据 */
        }
        
        /* 2. 写操作 */
        printf("[WRITE] ");
        ret = i2c_write(EM_I2C1, SLAVE_ADDR, TEST_REG_ADDR, EM_BOOL_FALSE, tx_buffer, TEST_LEN);
        if (ret > 0)
        {
            print_buffer("Sent", tx_buffer, TEST_LEN);
        }
        else
        {
            printf("FAILED (ret=%d)\n", ret);
            i2c_dump_regs(EM_I2C1);
            print_i2c_stats(EM_I2C1);
        }
        
        delay_ms(20);  /* 给从机处理时间 */
        
        /* 3. 读操作 */
        printf("[READ]  ");
        memset(rx_buffer, 0, sizeof(rx_buffer));
        ret = i2c_read(EM_I2C1, SLAVE_ADDR, TEST_REG_ADDR, EM_BOOL_FALSE, rx_buffer, TEST_LEN);
        if (ret > 0)
        {
            print_buffer("Recv", rx_buffer, TEST_LEN);
            if (memcmp(tx_buffer, rx_buffer, TEST_LEN) == 0)
            {
                printf("[VERIFY] PASS\n");
            }
            else
            {
                printf("[VERIFY] FAIL\n");
                print_i2c_stats(EM_I2C1);
            }
        }
        else
        {
            printf("FAILED (ret=%d)\n", ret);
            i2c_dump_regs(EM_I2C1);
            print_i2c_stats(EM_I2C1);
        }

        /* 4. 批量读操作（单事务连续读取） */
        printf("[BRD]   ");
        memset(rx_burst_buffer, 0, sizeof(rx_burst_buffer));
        ret = i2c_read_burst(EM_I2C1, SLAVE_ADDR, TEST_REG_ADDR, EM_BOOL_FALSE, rx_burst_buffer, TEST_LEN);
        if (ret > 0)
        {
            print_buffer("Recv", rx_burst_buffer, TEST_LEN);
            if (memcmp(tx_buffer, rx_burst_buffer, TEST_LEN) == 0)
            {
                printf("[B-VERIFY] PASS\n");
            }
            else
            {
                printf("[B-VERIFY] FAIL\n");
                print_i2c_stats(EM_I2C1);
            }
        }
        else
        {
            printf("FAILED (ret=%d)\n", ret);
            i2c_dump_regs(EM_I2C1);
            print_i2c_stats(EM_I2C1);
        }
        
        printf("\n");
        delay_ms(TEST_GAP_MS);
    }
}
