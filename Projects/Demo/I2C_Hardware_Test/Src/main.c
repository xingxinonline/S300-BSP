#include "s300.h"
#include "board.h"
#include "gpio.h"
#include "rcc.h"
#include "i2c.h"

#include <stdio.h>
#include <stdint.h>

#ifndef BOARD_I2C3_SCL_PIN
#define BOARD_I2C3_SCL_PIN      4
#endif

#ifndef BOARD_I2C3_SDA_PIN
#define BOARD_I2C3_SDA_PIN      5
#endif

#ifndef BOARD_I2C3_FUNCTION
#define BOARD_I2C3_FUNCTION     FUNCTION_3
#endif

#ifndef BOARD_I2C3_FREQ
#define BOARD_I2C3_FREQ         400000u
#endif

#ifndef BOARD_I2C3_ADDR_ES8311
#define BOARD_I2C3_ADDR_ES8311  0x18
#endif

#ifndef BOARD_I2C3_ADDR_OV5640
#define BOARD_I2C3_ADDR_OV5640  0x3C
#endif

#ifndef BOARD_I2C3_ADDR_ES7210
#define BOARD_I2C3_ADDR_ES7210  0x41
#endif

#ifndef BOARD_I2C3_ADDR_QMI8658A
#define BOARD_I2C3_ADDR_QMI8658A 0x6A
#endif

#define TEST_I2C                EM_I2C3
#define TEST_SCAN_INTERVAL_MS   5000u
#define PROBE_TIMEOUT_LOOPS     I2C_DEFAULT_TIMEOUT

typedef struct
{
    uint8_t addr;
    const char *name;
} i2c_known_device_t;

static const i2c_known_device_t g_known_devices[] = {
    { BOARD_I2C3_ADDR_ES8311, "ES8311 Audio Codec" },
    { BOARD_I2C3_ADDR_OV5640, "OV5640 Camera" },
    { BOARD_I2C3_ADDR_ES7210, "ES7210 Audio ADC" },
    { BOARD_I2C3_ADDR_QMI8658A, "QMI8658A IMU" },
};

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

static void i2c3_gpio_init(void)
{
    gpio_set_function(GPIOA, BOARD_I2C3_SCL_PIN, BOARD_I2C3_FUNCTION);
    gpio_set_mode(GPIOA, BOARD_I2C3_SCL_PIN, GPIO_UP);

    gpio_set_function(GPIOA, BOARD_I2C3_SDA_PIN, BOARD_I2C3_FUNCTION);
    gpio_set_mode(GPIOA, BOARD_I2C3_SDA_PIN, GPIO_UP);
}

static const char *lookup_device_name(uint8_t addr)
{
    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_known_devices) / sizeof(g_known_devices[0])); ++i)
    {
        if (g_known_devices[i].addr == addr)
        {
            return g_known_devices[i].name;
        }
    }
    return NULL;
}

static int i2c_probe_address(emI2C i2c, uint8_t addr)
{
    uint32_t timeout;

    if (addr < 0x08u || addr > 0x77u)
    {
        return 0;
    }

    timeout = PROBE_TIMEOUT_LOOPS;
    while (!(I2C_STATUS(i2c) & 0x4u) && timeout > 0u)
    {
        timeout--;
    }
    if (timeout == 0u)
    {
        return I2C_ERR_TIMEOUT;
    }

    set_i2c_state_clear(i2c);

    I2C_TAR(i2c) = ((uint32_t)addr & 0x3FFu) | 0x1000u;
    I2C_ENABLE(i2c) |= 1u;
    I2C_DATA_CMD(i2c) = CMD_DATA_READ | CMD_DATA_STOP;

    timeout = PROBE_TIMEOUT_LOOPS;
    while (timeout > 0u)
    {
        uint32_t raw_intr = I2C_RAW_INTR_STAT(i2c);

        if ((raw_intr & EM_I2C_TX_ABRT) != 0u)
        {
            break;
        }
        if ((raw_intr & EM_I2C_STOP_DET) != 0u)
        {
            break;
        }
        if (I2C_RXFLR(i2c) > 0u)
        {
            (void)I2C_DATA_CMD(i2c);
            break;
        }
        timeout--;
    }

    if (timeout == 0u)
    {
        set_i2c_state_clear(i2c);
        I2C_ENABLE(i2c) &= ~1u;
        return I2C_ERR_TIMEOUT;
    }

    timeout = PROBE_TIMEOUT_LOOPS;
    while ((I2C_STATUS(i2c) & 0x1u) && timeout > 0u)
    {
        timeout--;
    }

    {
        uint32_t abrt_src = I2C_TX_ABRT_SOURCE(i2c);
        set_i2c_state_clear(i2c);
        I2C_ENABLE(i2c) &= ~1u;

        if (timeout == 0u)
        {
            return I2C_ERR_TIMEOUT;
        }
        if (abrt_src != 0u)
        {
            return 0;
        }
    }

    return 1;
}

static uint32_t i2c_scan_bus(emI2C i2c, uint8_t *found_addrs, uint32_t max_found)
{
    uint32_t found = 0;

    printf("--- I2C Bus Scan ---\r\n");
    printf("    0 1 2 3 4 5 6 7 8 9 A B C D E F\r\n");

    for (uint8_t row = 0; row < 8u; ++row)
    {
        printf("%02X: ", (unsigned int)(row << 4));

        for (uint8_t col = 0; col < 16u; ++col)
        {
            uint8_t addr = (uint8_t)((row << 4) | col);
            int probe_ok;

            if (addr < 0x08u || addr > 0x77u)
            {
                continue;
            }

            probe_ok = i2c_probe_address(i2c, addr);
            if (probe_ok > 0)
            {
                printf("%02X ", addr);
                if (found < max_found)
                {
                    found_addrs[found] = addr;
                }
                found++;
            }
            else
            {
                printf("-- ");
            }
        }

        printf("\r\n");
    }

    printf("Found %lu device(s)\r\n\r\n", (unsigned long)found);

    for (uint32_t i = 0; i < found && i < max_found; ++i)
    {
        const char *name = lookup_device_name(found_addrs[i]);
        if (name != NULL)
        {
            printf("0x%02X: %s\r\n", found_addrs[i], name);
        }
        else
        {
            printf("0x%02X: Unknown\r\n", found_addrs[i]);
        }
    }
    printf("\r\n");

    return found;
}

static void test_qmi8658a_id(emI2C i2c, uint8_t addr)
{
    uint8_t who_am_i = 0;
    int ret;

    printf("Trying QMI8658A @ 0x%02X...\r\n", addr);

    if (i2c_probe_address(i2c, addr) <= 0)
    {
        printf("Device 0x%02X not found (err=-1)\r\n", addr);
        return;
    }

    ret = i2c_read(i2c, addr, 0x00, EM_BOOL_FALSE, &who_am_i, 1);
    if (ret > 0)
    {
        if (who_am_i == 0x05u)
        {
            printf("Device 0x%02X WHO_AM_I = 0x%02X (QMI8658A detected!)\r\n", addr, who_am_i);
        }
        else
        {
            printf("Device 0x%02X WHO_AM_I = 0x%02X (unexpected)\r\n", addr, who_am_i);
        }
    }
    else
    {
        printf("Read WHO_AM_I failed (ret=%d)\r\n", ret);
    }
}

static void test_ov5640_id(emI2C i2c, uint8_t addr)
{
    uint8_t id_high = 0;
    uint8_t id_low = 0;
    uint16_t chip_id;
    int ret_h;
    int ret_l;

    printf("Trying OV5640 @ 0x%02X...\r\n", addr);

    if (i2c_probe_address(i2c, addr) <= 0)
    {
        printf("Device 0x%02X not found (err=-1)\r\n", addr);
        return;
    }

    ret_h = i2c_read(i2c, addr, 0x300A, EM_BOOL_TRUE, &id_high, 1);
    ret_l = i2c_read(i2c, addr, 0x300B, EM_BOOL_TRUE, &id_low, 1);

    if (ret_h > 0 && ret_l > 0)
    {
        chip_id = (uint16_t)(((uint16_t)id_high << 8) | id_low);
        if (chip_id == 0x5640u)
        {
            printf("Device 0x%02X CHIP_ID = 0x%04X (OV5640 detected!)\r\n", addr, chip_id);
        }
        else
        {
            printf("Device 0x%02X CHIP_ID = 0x%04X (unexpected)\r\n", addr, chip_id);
        }
    }
    else
    {
        printf("Read CHIP_ID failed (ret_h=%d, ret_l=%d)\r\n", ret_h, ret_l);
    }
}

int main(void)
{
    int ret;
    uint32_t loop_count = 0;
    uint8_t found_addrs[32] = {0};
    uint32_t apb1_clock;

    board_init();

    printf("\r\nS300 Hardware I2C Driver Test\r\n");
    printf("Test I2C Port: I2C3\r\n");
    printf("SCL: GPIO%d, SDA: GPIO%d\r\n", BOARD_I2C3_SCL_PIN, BOARD_I2C3_SDA_PIN);
    printf("Speed: %lu Hz\r\n\r\n", (unsigned long)BOARD_I2C3_FREQ);

    rcc_set_cortex_m4_apb1_clock(RCC_CM4_APB1_I2C3, true);
    i2c3_gpio_init();

    apb1_clock = rcc_get_clock(RCC_CLOCK_APB1);
    printf("APB1 Clock: %lu Hz\r\n", (unsigned long)apb1_clock);

    ret = init_i2c(TEST_I2C,
                   (emI2CPRO)(EM_I2C_MASTER | EM_I2C_400K | EM_I2C_RESTART_EN),
                   0,
                   apb1_clock,
                   BOARD_I2C3_FREQ);
    if (ret != 0)
    {
        printf("I2C3 init failed, ret=%d\r\n", ret);
        while (1)
        {
            delay_ms(1000);
        }
    }

    i2c_set_timeout(I2C_DEFAULT_TIMEOUT);
    i2c_reset_stats(TEST_I2C);

    printf("I2C3 initialized successfully!\r\n\r\n");

    (void)i2c_scan_bus(TEST_I2C, found_addrs, (uint32_t)(sizeof(found_addrs) / sizeof(found_addrs[0])));

    printf("--- Read Device ID Test ---\r\n");
    test_qmi8658a_id(TEST_I2C, BOARD_I2C3_ADDR_QMI8658A);
    test_qmi8658a_id(TEST_I2C, (uint8_t)(BOARD_I2C3_ADDR_QMI8658A + 1u));
    test_ov5640_id(TEST_I2C, BOARD_I2C3_ADDR_OV5640);

    printf("\r\n--- Continuous Scan (every 5s) ---\r\n");

    while (1)
    {
        printf("[%lu] Scanning I2C3...\r\n", (unsigned long)loop_count);
        (void)i2c_scan_bus(TEST_I2C, found_addrs, (uint32_t)(sizeof(found_addrs) / sizeof(found_addrs[0])));
        loop_count++;
        delay_ms(TEST_SCAN_INTERVAL_MS);
    }
}
