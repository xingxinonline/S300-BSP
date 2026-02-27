/**
 * @file main.c
 * @brief I2C Slave Demo - 使用完善后的 BSP 从机 API
 */

#include "s300.h"
#include "board.h"
#include "gpio.h"
#include "rcc.h"
#include "i2c.h"
#include "version.h"

#include <stdio.h>
#include <string.h>

/*============================================================================
 * 配置
 *============================================================================*/

#define SLAVE_ADDR          0x03
#define SLAVE_MEM_SIZE      64      /* 模拟 EEPROM 大小 */
#define I2C_BUS_HZ          400000u

/* 打印级别: 0=静默 1=简洁(主循环打印) 2=详细(事后打印) */
#define PRINT_LEVEL         1

/*============================================================================
 * 从机数据 (模拟 EEPROM)
 *============================================================================*/

static uint8_t g_slave_mem[SLAVE_MEM_SIZE];  /* 模拟 EEPROM 数据 */
static uint8_t g_reg_addr = 0;               /* 当前寄存器地址 */
static uint8_t g_tx_index = 0;               /* 发送索引 */
static uint8_t g_first_byte = 1;             /* 第一个字节是地址 */

static volatile uint32_t g_rx_count = 0;     /* 当前传输收到的数据字节数 */
static volatile uint32_t g_tx_count = 0;     /* 当前传输发送的字节数 */
static volatile uint32_t g_trans_count = 0;  /* 完成的传输次数 */
static volatile uint32_t g_total_rx = 0;     /* 总接收字节数 */
static volatile uint32_t g_total_tx = 0;     /* 总发送字节数 */
static volatile uint8_t g_print_flag = 0;    /* 打印请求标志 */

/* 调试记录缓冲区 (延迟打印用) */
#if PRINT_LEVEL >= 2
#define DBG_LOG_SIZE    64
typedef struct {
    uint8_t type;       /* 0=empty, 'S'=start, 'W'=write, 'R'=read, 'P'=stop */
    uint8_t addr;       /* 地址或数据 */
    uint8_t data;       /* 数据 */
} dbg_entry_t;
static dbg_entry_t g_dbg_log[DBG_LOG_SIZE];
static volatile uint8_t g_dbg_idx = 0;

static inline void dbg_log(uint8_t type, uint8_t addr, uint8_t data)
{
    if (g_dbg_idx < DBG_LOG_SIZE) {
        g_dbg_log[g_dbg_idx].type = type;
        g_dbg_log[g_dbg_idx].addr = addr;
        g_dbg_log[g_dbg_idx].data = data;
        g_dbg_idx++;
    }
}
#endif

/*============================================================================
 * I2C 从机中断处理 (模拟 EEPROM 行为)
 * 
 * 重要: 中断中不能有 printf，否则会阻塞导致 FIFO 溢出！
 *============================================================================*/

/**
 * @brief I2C1 中断处理函数
 * 
 * 协议说明 (类似 24Cxx EEPROM):
 * - 写操作: [SlaveAddr+W][RegAddr][Data0][Data1]...
 *   第一个字节是寄存器地址，之后是数据
 * - 读操作: [SlaveAddr+W][RegAddr][SlaveAddr+R][Data0][Data1]...
 *   先写地址，再读数据
 */
void I2C1_IRQHandler(void)
{
    uint32_t status = I2C_INTR_STAT(EM_I2C1);
    
    /* START 条件 - 新传输开始 */
    if (status & EM_I2C_START_DET)
    {
        (void)I2C_CLR_START_DET(EM_I2C1);
        g_first_byte = 1;  /* 下一个 RX 是寄存器地址 */
#if PRINT_LEVEL >= 2
        dbg_log('S', 0, 0);
#endif
    }
    
    /* 主机写数据到从机 */
    if (status & EM_I2C_RX_FULL)
    {
        uint8_t data = i2c_slave_recv_byte(EM_I2C1);
        
        if (g_first_byte)
        {
            /* 第一个字节是寄存器地址 */
            g_reg_addr = data % SLAVE_MEM_SIZE;
            g_tx_index = g_reg_addr;
            g_first_byte = 0;
#if PRINT_LEVEL >= 2
            dbg_log('A', g_reg_addr, data);
#endif
        }
        else
        {
            /* 后续字节是数据，写入内存 */
            g_slave_mem[g_reg_addr] = data;
#if PRINT_LEVEL >= 2
            dbg_log('W', g_reg_addr, data);
#endif
            g_reg_addr = (g_reg_addr + 1) % SLAVE_MEM_SIZE;
            g_rx_count++;
        }
    }
    
    /* 主机请求读取数据 */
    if (status & EM_I2C_RD_REQ)
    {
        (void)I2C_CLR_RD_REQ(EM_I2C1);
        uint8_t send_data = g_slave_mem[g_tx_index];
        i2c_slave_send_byte(EM_I2C1, send_data);
#if PRINT_LEVEL >= 2
        dbg_log('R', g_tx_index, send_data);
#endif
        g_tx_index = (g_tx_index + 1) % SLAVE_MEM_SIZE;
        g_tx_count++;
    }
    
    /* 主机完成读取 */
    if (status & EM_I2C_RX_DONE)
    {
        (void)I2C_CLR_RX_DONE(EM_I2C1);
    }
    
    /* STOP 条件 - 传输结束 */
    if (status & EM_I2C_STOP_DET)
    {
        (void)I2C_CLR_STOP_DET(EM_I2C1);
        g_trans_count++;
        g_total_tx += g_tx_count;
        g_total_rx += g_rx_count;
#if PRINT_LEVEL >= 2
        dbg_log('P', g_tx_count, g_rx_count);
#endif
        /* 设置打印标志，主循环处理 */
        g_print_flag = 1;
        /* 重置当前传输计数 */
        g_tx_count = 0;
        g_rx_count = 0;
    }
    
    /* RESTART 条件 (如有) */
    if (status & EM_I2C_RESTART_DET)
    {
        (void)I2C_CLR_RESTART_DET(EM_I2C1);
    }
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void)
{
    board_init();
    
    /* 初始化模拟 EEPROM 数据 */
    memset(g_slave_mem, 0xFF, sizeof(g_slave_mem));

    printf("\n===========================================\n");
    printf("  S300 I2C Slave Demo (EEPROM Emulation)\n");
    printf("  %s @ %s\n", S300_VERSION_STRING, S300_BOARD_NAME);
    printf("  Build: %s\n", S300_BUILD_TIMESTAMP);
    printf("===========================================\n");
        printf("Slave: 0x%02X, Memory: %d bytes, I2C=%lu Hz\n",
            SLAVE_ADDR,
            SLAVE_MEM_SIZE,
            (unsigned long)I2C_BUS_HZ);
    printf("Protocol: [Addr+W][Reg][Data...] / [Addr+R][Data...]\n");

    /* 1. 使能 I2C1 时钟 */
    rcc_set_cortex_m4_apb1_clock(RCC_CM4_APB1_I2C1, true);

    /* 2. 配置 GPIO (I2C1 = GPIO0/1) */
    gpio_set_function(GPIOA, 0, FUNCTION_3);
    gpio_set_function(GPIOA, 1, FUNCTION_3);

    /* 3. 配置从机并初始化 */
    i2c_slave_config_t slave_cfg = {
        .slave_addr = SLAVE_ADDR,
        .speed = EM_I2C_400K,
        .intr_mask = I2C_SLAVE_DEFAULT_INTR_MASK,
        .callback = NULL,  /* 使用自定义 I2C1_IRQHandler */
        .user_data = NULL,
    };
    
    i2c_slave_init(EM_I2C1, &slave_cfg);

    /* 4. 使能中断 (NVIC + S300 INT_CTRL) */
    i2c_slave_irq_enable(EM_I2C1, true);

    /* 5. 打印状态 */
    printf("I2C1 Slave initialized:\n");
    printf("  SAR=0x%02lX, CON=0x%02lX, MASK=0x%04lX\n",
           (unsigned long)I2C_SAR(EM_I2C1),
           (unsigned long)I2C_CON(EM_I2C1),
           (unsigned long)I2C_INTR_MASK(EM_I2C1));
    printf("\nWaiting for master...\n");

    /* 主循环 - 处理打印 */
    while (1)
    {
        /* 检查是否有新传输完成 */
        if (g_print_flag)
        {
            g_print_flag = 0;
            
#if PRINT_LEVEL >= 1
            printf("[%lu] TX:%lu RX:%lu\n", 
                   (unsigned long)g_trans_count,
                   (unsigned long)g_total_tx,
                   (unsigned long)g_total_rx);
#endif

#if PRINT_LEVEL >= 2
            /* 打印详细日志 */
            printf("  Log: ");
            for (uint8_t i = 0; i < g_dbg_idx; i++)
            {
                switch (g_dbg_log[i].type)
                {
                    case 'S': printf("[S]"); break;
                    case 'A': printf("[A:%02X]", g_dbg_log[i].addr); break;
                    case 'W': printf("[W%02X=%02X]", g_dbg_log[i].addr, g_dbg_log[i].data); break;
                    case 'R': printf("[R%02X=%02X]", g_dbg_log[i].addr, g_dbg_log[i].data); break;
                    case 'P': printf("[P:TX%d,RX%d]", g_dbg_log[i].addr, g_dbg_log[i].data); break;
                }
            }
            printf("\n");
            g_dbg_idx = 0;  /* 清空日志 */
#endif
        }
        
        __WFI();
    }
}
