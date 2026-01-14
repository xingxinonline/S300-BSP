#include <stdio.h>
#include <stdint.h>
#include "s300.h"
#include "psram.h"
#include "board.h"
#include "rcc.h"

#define PSRAM_BASE_ADDR 0x80000000
#define TEST_SIZE       (1024 * 1024) // 1MB test

static volatile uint32_t g_tick_ms = 0;

void SysTick_Handler(void)
{
    g_tick_ms++;
}

static uint32_t millis(void)
{
    return g_tick_ms;
}

void psram_test_16bit(void)
{
    volatile uint16_t *psram16 = (volatile uint16_t *)PSRAM_BASE_ADDR;
    uint32_t i;
    uint16_t write_val;
    uint16_t read_val;
    int errors = 0;
    uint32_t start_time, end_time;
    float duration_s;
    float speed_mbps;

    printf("\r\n=== Starting PSRAM 16-bit Performance & Stress Test ===\r\n");
    printf("Test Size: %d Bytes (%.2f MB)\r\n", TEST_SIZE, (float)TEST_SIZE / (1024*1024));

    // Write test
    printf("Writing data (16-bit)...\r\n");
    start_time = millis();
    for (i = 0; i < TEST_SIZE / 2; i++) {
        psram16[i] = (uint16_t)(i & 0xFFFF);
    }
    end_time = millis();
    duration_s = (end_time - start_time) / 1000.0f;
    speed_mbps = (TEST_SIZE / (1024.0f * 1024.0f)) / duration_s;
    printf("Write Complete. Time: %lu ms, Speed: %.2f MB/s\r\n", (end_time - start_time), speed_mbps);

    // Read and verify test
    printf("Reading and verifying data (16-bit)...\r\n");
    start_time = millis();
    for (i = 0; i < TEST_SIZE / 2; i++) {
        write_val = (uint16_t)(i & 0xFFFF);
        read_val = psram16[i];

        if (read_val != write_val) {
            errors++;
            if (errors <= 10) {
                printf("Error at offset 0x%08X: Expected 0x%04X, Read 0x%04X\r\n", 
                       (unsigned int)(i * 2), write_val, read_val);
            }
        }
    }
    end_time = millis();
    duration_s = (end_time - start_time) / 1000.0f;
    speed_mbps = (TEST_SIZE / (1024.0f * 1024.0f)) / duration_s;
    printf("Read Complete. Time: %lu ms, Speed: %.2f MB/s\r\n", (end_time - start_time), speed_mbps);

    if (errors == 0) {
        printf("PSRAM 16-bit Test Passed!\r\n");
    } else {
        printf("PSRAM 16-bit Test Failed with %d errors.\r\n", errors);
    }
}

void psram_test_32bit(void)
{
    volatile uint32_t *psram32 = (volatile uint32_t *)PSRAM_BASE_ADDR;
    uint32_t i;
    uint32_t val;
    int errors = 0;
    uint32_t start_time, end_time;

    printf("\r\n=== Starting PSRAM 32-bit Access Test ===\r\n");
    printf("Note: If PSRAM controller only supports 16-bit, this relies on AHB bridge splitting.\r\n");

    // Write test
    printf("Writing data (32-bit)...\r\n");
    start_time = millis();
    for (i = 0; i < TEST_SIZE / 4; i++) {
        psram32[i] = (i * 0x12345678) + i;
    }
    end_time = millis();
    printf("Write Complete. Time: %lu ms\r\n", (end_time - start_time));

    // Read and verify
    printf("Reading and verifying data (32-bit)...\r\n");
    for (i = 0; i < TEST_SIZE / 4; i++) {
        uint32_t expected = (i * 0x12345678) + i;
        val = psram32[i];
        if (val != expected) {
            errors++;
            if (errors <= 10) {
                printf("Error at offset 0x%08X: Expected 0x%08X, Read 0x%08X\r\n", 
                       (unsigned int)(i * 4), expected, val);
            }
        }
    }

    if (errors == 0) {
        printf("PSRAM 32-bit Test Passed!\r\n");
    } else {
        printf("PSRAM 32-bit Test Failed with %d errors.\r\n", errors);
    }
}

int main(void)
{
    board_init();
    printf("\r\n[S300][PSRAM_Test] Booting...\r\n");

    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U) {
        printf("[ERR] SysTick_Config failed!\r\n");
    }

    rcc_init_mm_pll(8, 400, 0, 3, 2); /* 100MHz */
    rcc_init_dsp_pll(6, 768, 0, 2, 2); /* 300MHz */

    // Initialize PSRAM with parameters used in Display_Demo
    init_psram(4, 1);

    int loop_count = 0;
    while(1) {
        printf("\r\n\r\n>>> Test Loop %d <<<\r\n", ++loop_count);
        
        psram_test_16bit();
        
        psram_test_32bit();

        printf("\r\nSkipping 8-bit test (known to hang bus).\r\n");
        
        // Delay between loops
        uint32_t t0 = millis();
        while((millis() - t0) < 2000);
    }
    return 0;
}
