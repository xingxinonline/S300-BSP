/**
 * @file i2c.c
 * @brief S300 硬件 I2C 驱动实现 (直接移植SDK)
 * 
 * 基于 DesignWare DW_apb_i2c 控制器
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "i2c.h"
#include "s300.h"  /* CMSIS NVIC functions */

/* -------------------- 内部变量 -------------------- */

static uint32_t gu_config[4] = {0};
static i2c_stats_t g_i2c_stats[4] = {0};

/** 全局超时计数 */
static uint32_t g_i2c_timeout = I2C_DEFAULT_TIMEOUT;

/* -------------------- 超时配置 -------------------- */

void i2c_set_timeout(uint32_t timeout_loops)
{
    g_i2c_timeout = timeout_loops;
}

uint32_t i2c_get_timeout(void)
{
    return g_i2c_timeout;
}

/* -------------------- 内部辅助宏 -------------------- */

/** 等待条件满足，带超时 */
#define I2C_WAIT_TIMEOUT(cond, timeout_var) \
    do { \
        timeout_var = g_i2c_timeout; \
        while (!(cond) && timeout_var > 0) { \
            timeout_var--; \
        } \
    } while(0)

/** 检查是否发生 TX_ABRT */
#define I2C_CHECK_ABORT(i2c) ((I2C_RAW_INTR_STAT(i2c) & EM_I2C_TX_ABRT) != 0)

static inline bool i2c_valid_idx(emI2C i2c)
{
    return ((uint32_t)i2c <= (uint32_t)EM_I2C3);
}

static void i2c_record_success(emI2C i2c, bool is_read, uint32_t bytes)
{
    if (!i2c_valid_idx(i2c))
    {
        return;
    }

    if (is_read)
    {
        g_i2c_stats[i2c].rx_ok_count++;
        g_i2c_stats[i2c].rx_ok_bytes += bytes;
    }
    else
    {
        g_i2c_stats[i2c].tx_ok_count++;
        g_i2c_stats[i2c].tx_ok_bytes += bytes;
    }
    g_i2c_stats[i2c].last_error = I2C_OK;
}

static void i2c_record_error(emI2C i2c, i2c_err_t err, uint32_t abrt_source)
{
    if (!i2c_valid_idx(i2c))
    {
        return;
    }

    g_i2c_stats[i2c].last_error = (int32_t)err;
    g_i2c_stats[i2c].last_abort_source = abrt_source;

    switch (err)
    {
        case I2C_ERR_TIMEOUT:
            g_i2c_stats[i2c].timeout_count++;
            break;
        case I2C_ERR_NACK:
            g_i2c_stats[i2c].nack_count++;
            break;
        case I2C_ERR_ARB_LOST:
            g_i2c_stats[i2c].arb_lost_count++;
            break;
        case I2C_ERR_ABORT:
            g_i2c_stats[i2c].abort_count++;
            break;
        default:
            break;
    }
}

void i2c_reset_stats(emI2C i2c)
{
    if (!i2c_valid_idx(i2c))
    {
        return;
    }

    memset(&g_i2c_stats[i2c], 0, sizeof(g_i2c_stats[i2c]));
}

void i2c_get_stats(emI2C i2c, i2c_stats_t *stats)
{
    if (!i2c_valid_idx(i2c) || stats == NULL)
    {
        return;
    }

    *stats = g_i2c_stats[i2c];
}

/* -------------------- SDK API 实现 -------------------- */

/**
 * @brief 初始化 I2C (完全移植SDK)
 */
int init_i2c(emI2C i2c, emI2CPRO pro, uint16_t saddr, uint32_t apbclock, uint32_t i2cclock)
{
    uint32_t temp;
    uint32_t h, l;
    
    gu_config[i2c] = pro;
    temp = pro & 0xFFu;
    
    set_i2c_enable(i2c, EM_BOOL_FALSE);
    
    I2C_CON(i2c) = temp;
    I2C_INTR_MASK(i2c) = 0;
    set_i2c_state_clear(i2c);
    
    temp = saddr & 0x3FFu;
    if (pro & (EM_I2C_MASTER | EM_I2C_MASTER_10BIT))
    {
        temp |= 0x1000u;  /* IC_TAR@12bit */
    }
    temp |= pro & (EM_I2C_SPECIAL | EM_I2C_GC_OR_START);  /* IC_TAR@11 10bit */
    I2C_TAR(i2c) = temp;
    
    temp = apbclock / i2cclock;
    h = temp * 6u / 10u;
    l = temp * 4u / 10u;
    
    if (pro & EM_I2C_100K)
    {
        I2C_SS_SCL_HCNT(i2c) = h;
        I2C_SS_SCL_LCNT(i2c) = l;
    }
    else if (pro & EM_I2C_400K)
    {
        I2C_FS_SCL_HCNT(i2c) = h;
        I2C_FS_SCL_LCNT(i2c) = l;
    }
    else
    {
        I2C_HS_SCL_HCNT(i2c) = h;
        I2C_HS_SCL_LCNT(i2c) = l;
    }
    
    temp = 0x000u;
    if (pro & EM_I2C_INTERRUPT)
    {
        temp = 0x3FFFu;
    }
    I2C_INTR_MASK(i2c) = temp;
    
    I2C_RX_TL(i2c) = 0;
    I2C_TX_TL(i2c) = 0;
    I2C_FS_SPKLEN(i2c) = 10;
    I2C_HS_SPKLEN(i2c) = 10;
    
    I2C_ENABLE(i2c) |= 1u;
    
    return 0;
}

/**
 * @brief I2C 写操作 (完全移植SDK - 不做任何修改)
 */
int write_i2c(emI2C i2c, uint16_t saddr, uint16_t address, emBoolean is16bit, uint8_t *data, uint16_t dlen)
{
    uint32_t temp;
    int i = 0;
    
    /* 等待 TX FIFO 空 */
    while (!(I2C_STATUS(i2c) & 0x4u))
    {
        /* wait */
    }
    
    /* SDK原始代码：不禁用I2C！ */
    // set_i2c_enable(i2c, EM_BOOL_FALSE);
    set_i2c_state_clear(i2c);
    
    temp = saddr & 0x3FFu;
    if (I2C_CON(i2c) & (EM_I2C_MASTER | EM_I2C_MASTER_10BIT))
    {
        temp |= 0x1000u;  /* IC_TAR@12bit */
    }
    temp |= gu_config[i2c] & (EM_I2C_SPECIAL | EM_I2C_GC_OR_START);  /* IC_TAR@11 10bit */
    I2C_TAR(i2c) = temp;
    I2C_ENABLE(i2c) |= 1u;
    
    if (is16bit)
    {
        I2C_DATA_CMD(i2c) = (address >> 8) & 0xFFu;
        while (!(I2C_STATUS(i2c) & 0x2u))
        {
            /* wait TX FIFO not full */
        }
    }
    I2C_DATA_CMD(i2c) = address & 0xFFu;
    
    for (i = 0; i < dlen - 1; i++)
    {
        while (!(I2C_STATUS(i2c) & 0x2u))
        {
            /* wait */
        }
        I2C_DATA_CMD(i2c) = data[i];
    }
    
    while (!(I2C_STATUS(i2c) & 0x2u))
    {
        /* wait */
    }
    I2C_DATA_CMD(i2c) = data[i] | CMD_DATA_STOP;
    i++;
    
    /* 等待传输完成 */
    while (!(I2C_STATUS(i2c) & 0x4u))
    {
        /* wait TX FIFO empty */
    }
    while (!(I2C_RAW_INTR_STAT(i2c) & 0x200u))
    {
        /* wait STOP_DET */
    }
    while ((I2C_STATUS(i2c) & 0x1u))
    {
        /* wait activity clear */
    }
    
    set_i2c_state_clear(i2c);
    I2C_ENABLE(i2c) &= ~1u;
    
    return i;
}

/**
 * @brief I2C 读操作 (完全移植SDK - 不做任何修改)
 */
int read_i2c(emI2C i2c, uint16_t saddr, uint16_t address, emBoolean is16bit, uint8_t *data, uint16_t dlen)
{
    uint32_t temp;
    int i = 0;
    
    /* 等待 TX FIFO 空 */
    while (!(I2C_STATUS(i2c) & 0x4u))
    {
        /* wait */
    }
    
    /* SDK原始代码：不禁用I2C！ */
    // set_i2c_enable(i2c, EM_BOOL_FALSE);
    set_i2c_state_clear(i2c);
    
    temp = saddr & 0x3FFu;
    if (I2C_CON(i2c) & (EM_I2C_MASTER | EM_I2C_MASTER_10BIT))
    {
        temp |= 0x1000u;  /* IC_TAR@12bit */
    }
    temp |= gu_config[i2c] & (EM_I2C_SPECIAL | EM_I2C_GC_OR_START);  /* IC_TAR@11 10bit */
    I2C_TAR(i2c) = temp;
    I2C_ENABLE(i2c) |= 1u;
    
    if (is16bit)
    {
        I2C_DATA_CMD(i2c) = (address >> 8) & 0xFFu;
        while (!(I2C_STATUS(i2c) & 0x2u))
        {
            /* wait */
        }
        I2C_DATA_CMD(i2c) = address & 0xFFu;
        while (!(I2C_STATUS(i2c) & 0x2u))
        {
            /* wait */
        }
    }
    else
    {
        I2C_DATA_CMD(i2c) = (address & 0xFFu) | CMD_DATA_STOP;
        while (!(I2C_STATUS(i2c) & 0x2u))
        {
            /* wait */
        }
    }
    
    for (i = 0; i < dlen; i++)
    {
        I2C_DATA_CMD(i2c) = CMD_DATA_READ;
        while (!(I2C_STATUS(i2c) & 0x8u))
        {
            /* wait RX FIFO not empty */
        }
        data[i] = I2C_DATA_CMD(i2c) & 0xFFu;
        while (!(I2C_STATUS(i2c) & 0x4u))
        {
            /* wait TX FIFO empty */
        }
    }
    
    /* 等待传输完成 */
    while (!(I2C_STATUS(i2c) & 0x4u))
    {
        /* wait */
    }
    while (!(I2C_RAW_INTR_STAT(i2c) & 0x200u))
    {
        /* wait STOP_DET */
    }
    while ((I2C_STATUS(i2c) & 0x1u))
    {
        /* wait */
    }
    
    set_i2c_state_clear(i2c);
    I2C_ENABLE(i2c) &= ~1u;
    
    return i;
}

/**
 * @brief 使能/禁用 I2C (完全移植SDK)
 */
void set_i2c_enable(emI2C i2c, emBoolean en)
{
    if (!en)
    {
        I2C_ENABLE(i2c) &= ~1u;
        while ((I2C_ENABLE_STATUS(i2c) & 0x1u))
        {
            /* wait disable complete */
        }
    }
    else
    {
        I2C_ENABLE(i2c) |= 1u;
    }
}

/**
 * @brief 设置 I2C 中断 (完全移植SDK)
 */
void set_i2c_interrupt(emI2C i2c, emI2CINT value)
{
    set_i2c_enable(i2c, EM_BOOL_FALSE);
    I2C_INTR_MASK(i2c) = value;
    set_i2c_enable(i2c, EM_BOOL_TRUE);
}

/**
 * @brief 发送 STOP 条件 (完全移植SDK)
 */
void set_i2c_stop(emI2C i2c)
{
    I2C_DATA_CMD(i2c) = CMD_DATA_STOP;
}

/**
 * @brief 清除 I2C 中断状态 (完全移植SDK)
 */
void set_i2c_state_clear(emI2C i2c)
{
    volatile uint32_t temp;
    temp = I2C_INTR_STAT(i2c);
    temp = I2C_CLR_INTR(i2c);
    temp = I2C_CLR_RX_UNDER(i2c);
    temp = I2C_CLR_RX_OVER(i2c);
    temp = I2C_CLR_TX_OVER(i2c);
    temp = I2C_CLR_RD_REQ(i2c);
    temp = I2C_CLR_TX_ABRT(i2c);
    temp = I2C_CLR_RX_DONE(i2c);
    temp = I2C_CLR_ACTIVITY(i2c);
    temp = I2C_CLR_STOP_DET(i2c);
    temp = I2C_CLR_START_DET(i2c);
    temp = I2C_CLR_GEN_CALL(i2c);
    temp = I2C_CLR_RESTART_DET(i2c);
    (void)temp;
}

/**
 * @brief 打印 I2C 寄存器状态 (调试用)
 */
void i2c_dump_regs(emI2C i2c)
{
    printf("[I2C%d] CON=0x%08lX TAR=0x%08lX SAR=0x%08lX\n",
           i2c,
           (unsigned long)I2C_CON(i2c),
           (unsigned long)I2C_TAR(i2c),
           (unsigned long)I2C_SAR(i2c));
    printf("[I2C%d] ENABLE=0x%08lX EN_STAT=0x%08lX STATUS=0x%08lX\n",
           i2c,
           (unsigned long)I2C_ENABLE(i2c),
           (unsigned long)I2C_ENABLE_STATUS(i2c),
           (unsigned long)I2C_STATUS(i2c));
    printf("[I2C%d] INTR_STAT=0x%08lX RAW_INTR=0x%08lX MASK=0x%08lX\n",
           i2c,
           (unsigned long)I2C_INTR_STAT(i2c),
           (unsigned long)I2C_RAW_INTR_STAT(i2c),
           (unsigned long)I2C_INTR_MASK(i2c));
    printf("[I2C%d] TXFLR=%lu RXFLR=%lu TX_ABRT=0x%08lX\n",
           i2c,
           (unsigned long)I2C_TXFLR(i2c),
           (unsigned long)I2C_RXFLR(i2c),
           (unsigned long)I2C_TX_ABRT_SOURCE(i2c));
}

/* -------------------- I2C 从机模式 API 实现 -------------------- */

/** 从机回调函数存储 */
static i2c_slave_callback_t g_slave_callback[4] = {NULL};
static void *g_slave_user_data[4] = {NULL};

/**
 * @brief 获取 I2C 对应的 IRQ 号
 */
static inline uint32_t i2c_get_irq_num(emI2C i2c)
{
    return S300_IRQ_I2C0 + (uint32_t)i2c;
}

/**
 * @brief 初始化 I2C 从机模式
 * 
 * 完全按照 SDK demo_i2c_slave 的方式实现：
 * 1. 禁用 I2C
 * 2. 设置 SAR (从机地址)
 * 3. 设置 CON (从机模式 + 速度)
 * 4. 设置 INTR_MASK
 * 5. 使能 I2C
 */
int i2c_slave_init(emI2C i2c, const i2c_slave_config_t *config)
{
    if (i2c > EM_I2C3 || config == NULL)
    {
        return -1;
    }
    
    /* 保存回调函数 */
    g_slave_callback[i2c] = config->callback;
    g_slave_user_data[i2c] = config->user_data;
    
    /* 禁用 I2C */
    set_i2c_enable(i2c, EM_BOOL_FALSE);
    
    /* 设置从机地址 */
    I2C_SAR(i2c) = config->slave_addr & 0x7Fu;
    
    /* 设置控制寄存器：从机模式 + 速度 */
    I2C_CON(i2c) = EM_I2C_SLAVE | (config->speed & 0x06u);
    
    /* 设置中断掩码 */
    I2C_INTR_MASK(i2c) = config->intr_mask;
    
    /* 使能 I2C */
    set_i2c_enable(i2c, EM_BOOL_TRUE);
    
    return 0;
}

/**
 * @brief 使能 I2C 从机中断
 * 
 * S300 需要配置两级中断：
 * 1. NVIC (标准 ARM)
 * 2. INT_CTRL (S300 特有的中断路由)
 */
void i2c_slave_irq_enable(emI2C i2c, bool enable)
{
    uint32_t irq_num = i2c_get_irq_num(i2c);
    uint8_t shift = irq_num % 32u;
    uint32_t mask_bit = 1u << shift;
    uint8_t reg_idx = irq_num >> 5;
    
    if (enable)
    {
        /* 1. 清除 NVIC pending 并使能 */
        NVIC_ClearPendingIRQ(irq_num);
        NVIC_EnableIRQ(irq_num);
        
        /* 2. 清除 S300 INT_CTRL mask (使能中断路由) */
        S300_ARM_INT_MASK(reg_idx) &= ~mask_bit;
    }
    else
    {
        /* 1. 禁用 NVIC */
        NVIC_DisableIRQ(irq_num);
        
        /* 2. 设置 S300 INT_CTRL mask (禁用中断路由) */
        S300_ARM_INT_MASK(reg_idx) |= mask_bit;
    }
}

/**
 * @brief 通用 I2C 从机中断处理 (内部使用)
 * 
 * 如果用户注册了回调函数，会调用回调函数处理。
 * 否则用户需要自己实现 I2Cx_IRQHandler。
 */
static void i2c_slave_irq_handler_internal(emI2C i2c)
{
    uint32_t status = I2C_INTR_STAT(i2c);
    
    if (g_slave_callback[i2c] != NULL)
    {
        g_slave_callback[i2c](i2c, status, g_slave_user_data[i2c]);
    }
}

/**
 * @brief 默认的 I2C 从机中断处理函数 (供弱链接)
 * 
 * 用户可以重写这些函数，或者在 i2c_slave_init 时注册回调函数。
 */
__attribute__((weak)) void I2C0_IRQHandler(void)
{
    i2c_slave_irq_handler_internal(EM_I2C0);
}

__attribute__((weak)) void I2C2_IRQHandler(void)
{
    i2c_slave_irq_handler_internal(EM_I2C2);
}

__attribute__((weak)) void I2C3_IRQHandler(void)
{
    i2c_slave_irq_handler_internal(EM_I2C3);
}

__attribute__((weak)) void I2C1_IRQHandler(void)
{
    i2c_slave_irq_handler_internal(EM_I2C1);
}

/* -------------------- 增强型 API (带超时和错误处理) -------------------- */

/**
 * @brief 获取 TX_ABRT 原因
 */
uint32_t i2c_get_abort_source(emI2C i2c)
{
    return I2C_TX_ABRT_SOURCE(i2c);
}

/**
 * @brief 清除 TX_ABRT 状态
 */
void i2c_clear_abort(emI2C i2c)
{
    (void)I2C_CLR_TX_ABRT(i2c);
}

/**
 * @brief I2C 软恢复
 */
int i2c_recover_bus(emI2C i2c)
{
    if (!i2c_valid_idx(i2c))
    {
        return I2C_ERR_PARAM;
    }

    set_i2c_enable(i2c, EM_BOOL_FALSE);
    set_i2c_state_clear(i2c);
    i2c_clear_abort(i2c);
    set_i2c_enable(i2c, EM_BOOL_TRUE);

    g_i2c_stats[i2c].recover_count++;
    return I2C_OK;
}

/**
 * @brief I2C 写操作 (带超时和错误检测)
 */
int i2c_write(emI2C i2c, uint16_t saddr, uint16_t address, emBoolean is16bit, uint8_t *data, uint16_t dlen)
{
    uint32_t temp;
    uint32_t timeout;
    int i = 0;
    
    /* 参数检查 */
    if (!i2c_valid_idx(i2c))
    {
        return I2C_ERR_PARAM;
    }
    if (data == NULL || dlen == 0)
    {
        return I2C_ERR_PARAM;
    }
    
    /* 等待 TX FIFO 空 */
    I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x4u, timeout);
    if (timeout == 0)
    {
        return I2C_ERR_TIMEOUT;
    }
    
    /* 清除状态 */
    set_i2c_state_clear(i2c);
    
    /* 设置目标地址 */
    temp = saddr & 0x3FFu;
    if (I2C_CON(i2c) & (EM_I2C_MASTER | EM_I2C_MASTER_10BIT))
    {
        temp |= 0x1000u;
    }
    temp |= gu_config[i2c] & (EM_I2C_SPECIAL | EM_I2C_GC_OR_START);
    I2C_TAR(i2c) = temp;
    I2C_ENABLE(i2c) |= 1u;
    
    /* 发送地址 */
    if (is16bit)
    {
        I2C_DATA_CMD(i2c) = (address >> 8) & 0xFFu;
        I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x2u, timeout);
        if (timeout == 0 || I2C_CHECK_ABORT(i2c))
        {
            goto abort_exit;
        }
    }
    I2C_DATA_CMD(i2c) = address & 0xFFu;
    
    /* 发送数据 */
    for (i = 0; i < dlen - 1; i++)
    {
        I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x2u, timeout);
        if (timeout == 0 || I2C_CHECK_ABORT(i2c))
        {
            goto abort_exit;
        }
        I2C_DATA_CMD(i2c) = data[i];
    }
    
    /* 最后一个字节带 STOP */
    I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x2u, timeout);
    if (timeout == 0 || I2C_CHECK_ABORT(i2c))
    {
        goto abort_exit;
    }
    I2C_DATA_CMD(i2c) = data[i] | CMD_DATA_STOP;
    i++;
    
    /* 等待完成 */
    I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x4u, timeout);
    if (timeout == 0)
    {
        goto timeout_exit;
    }
    
    I2C_WAIT_TIMEOUT(I2C_RAW_INTR_STAT(i2c) & 0x200u, timeout);
    if (timeout == 0)
    {
        goto timeout_exit;
    }
    
    I2C_WAIT_TIMEOUT(!(I2C_STATUS(i2c) & 0x1u), timeout);
    
    set_i2c_state_clear(i2c);
    I2C_ENABLE(i2c) &= ~1u;

    i2c_record_success(i2c, false, (uint32_t)i);
    
    return i;

abort_exit:
    {
        uint32_t abrt_src = I2C_TX_ABRT_SOURCE(i2c);
        i2c_err_t err = I2C_ERR_ABORT;
        set_i2c_state_clear(i2c);
        I2C_ENABLE(i2c) &= ~1u;
        
        if (abrt_src & 0x0001u) /* ABRT_7B_ADDR_NOACK */
            err = I2C_ERR_NACK;
        if (abrt_src & 0x0002u) /* ABRT_10ADDR1_NOACK */
            err = I2C_ERR_NACK;
        if (abrt_src & 0x0010u) /* ABRT_GCALL_NOACK */
            err = I2C_ERR_NACK;
        if (abrt_src & 0x1000u) /* ABRT_SLV_ARBLOST */
            err = I2C_ERR_ARB_LOST;

        i2c_record_error(i2c, err, abrt_src);
        if (err == I2C_ERR_ARB_LOST || err == I2C_ERR_ABORT)
        {
            (void)i2c_recover_bus(i2c);
        }
        return err;
    }

timeout_exit:
    set_i2c_state_clear(i2c);
    I2C_ENABLE(i2c) &= ~1u;
    i2c_record_error(i2c, I2C_ERR_TIMEOUT, 0);
    (void)i2c_recover_bus(i2c);
    return I2C_ERR_TIMEOUT;
}

/**
 * @brief I2C 读操作 (带超时和错误检测)
 */
int i2c_read(emI2C i2c, uint16_t saddr, uint16_t address, emBoolean is16bit, uint8_t *data, uint16_t dlen)
{
    uint32_t temp;
    uint32_t timeout;
    int i = 0;
    
    /* 参数检查 */
    if (!i2c_valid_idx(i2c))
    {
        return I2C_ERR_PARAM;
    }
    if (data == NULL || dlen == 0)
    {
        return I2C_ERR_PARAM;
    }
    
    /* 等待 TX FIFO 空 */
    I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x4u, timeout);
    if (timeout == 0)
    {
        return I2C_ERR_TIMEOUT;
    }
    
    /* 清除状态 */
    set_i2c_state_clear(i2c);
    
    /* 设置目标地址 */
    temp = saddr & 0x3FFu;
    if (I2C_CON(i2c) & (EM_I2C_MASTER | EM_I2C_MASTER_10BIT))
    {
        temp |= 0x1000u;
    }
    temp |= gu_config[i2c] & (EM_I2C_SPECIAL | EM_I2C_GC_OR_START);
    I2C_TAR(i2c) = temp;
    I2C_ENABLE(i2c) |= 1u;
    
    /* 发送寄存器地址 */
    if (is16bit)
    {
        I2C_DATA_CMD(i2c) = (address >> 8) & 0xFFu;
        I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x2u, timeout);
        if (timeout == 0 || I2C_CHECK_ABORT(i2c))
        {
            goto abort_exit;
        }
        I2C_DATA_CMD(i2c) = address & 0xFFu;
        I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x2u, timeout);
        if (timeout == 0 || I2C_CHECK_ABORT(i2c))
        {
            goto abort_exit;
        }
    }
    else
    {
        I2C_DATA_CMD(i2c) = (address & 0xFFu) | CMD_DATA_STOP;
        I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x2u, timeout);
        if (timeout == 0 || I2C_CHECK_ABORT(i2c))
        {
            goto abort_exit;
        }
    }
    
    /* 读取数据 */
    for (i = 0; i < dlen; i++)
    {
        I2C_DATA_CMD(i2c) = CMD_DATA_READ;
        
        I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x8u, timeout);
        if (timeout == 0 || I2C_CHECK_ABORT(i2c))
        {
            goto abort_exit;
        }
        
        data[i] = I2C_DATA_CMD(i2c) & 0xFFu;
        
        I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x4u, timeout);
        if (timeout == 0)
        {
            goto timeout_exit;
        }
    }
    
    /* 等待完成 */
    I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x4u, timeout);
    if (timeout == 0)
    {
        goto timeout_exit;
    }
    I2C_WAIT_TIMEOUT(I2C_RAW_INTR_STAT(i2c) & 0x200u, timeout);
    if (timeout == 0)
    {
        goto timeout_exit;
    }
    I2C_WAIT_TIMEOUT(!(I2C_STATUS(i2c) & 0x1u), timeout);
    if (timeout == 0)
    {
        goto timeout_exit;
    }
    
    set_i2c_state_clear(i2c);
    I2C_ENABLE(i2c) &= ~1u;

    i2c_record_success(i2c, true, (uint32_t)i);
    
    return i;

abort_exit:
    {
        uint32_t abrt_src = I2C_TX_ABRT_SOURCE(i2c);
        i2c_err_t err = I2C_ERR_ABORT;
        set_i2c_state_clear(i2c);
        I2C_ENABLE(i2c) &= ~1u;
        
        if (abrt_src & 0x0001u)
            err = I2C_ERR_NACK;
        if (abrt_src & 0x0002u)
            err = I2C_ERR_NACK;
        if (abrt_src & 0x1000u)
            err = I2C_ERR_ARB_LOST;

        i2c_record_error(i2c, err, abrt_src);
        if (err == I2C_ERR_ARB_LOST || err == I2C_ERR_ABORT)
        {
            (void)i2c_recover_bus(i2c);
        }
        return err;
    }

timeout_exit:
    set_i2c_state_clear(i2c);
    I2C_ENABLE(i2c) &= ~1u;
    i2c_record_error(i2c, I2C_ERR_TIMEOUT, 0);
    (void)i2c_recover_bus(i2c);
    return I2C_ERR_TIMEOUT;
}

/**
 * @brief I2C 批量读操作（单次事务连续读取）
 */
int i2c_read_burst(emI2C i2c, uint16_t saddr, uint16_t address, emBoolean is16bit, uint8_t *data, uint16_t dlen)
{
    uint32_t temp;
    uint32_t timeout;
    int i;

    if (!i2c_valid_idx(i2c))
    {
        return I2C_ERR_PARAM;
    }
    if (data == NULL || dlen == 0)
    {
        return I2C_ERR_PARAM;
    }

    I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x4u, timeout);
    if (timeout == 0)
    {
        i2c_record_error(i2c, I2C_ERR_TIMEOUT, 0);
        return I2C_ERR_TIMEOUT;
    }

    set_i2c_state_clear(i2c);

    temp = saddr & 0x3FFu;
    if (I2C_CON(i2c) & (EM_I2C_MASTER | EM_I2C_MASTER_10BIT))
    {
        temp |= 0x1000u;
    }
    temp |= gu_config[i2c] & (EM_I2C_SPECIAL | EM_I2C_GC_OR_START);
    I2C_TAR(i2c) = temp;
    I2C_ENABLE(i2c) |= 1u;

    if (is16bit)
    {
        I2C_DATA_CMD(i2c) = (address >> 8) & 0xFFu;
        I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x2u, timeout);
        if (timeout == 0 || I2C_CHECK_ABORT(i2c))
        {
            goto abort_exit;
        }
    }
    I2C_DATA_CMD(i2c) = address & 0xFFu;
    I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x2u, timeout);
    if (timeout == 0 || I2C_CHECK_ABORT(i2c))
    {
        goto abort_exit;
    }

    for (i = 0; i < (int)dlen; i++)
    {
        uint32_t cmd = CMD_DATA_READ;
        if (i == ((int)dlen - 1))
        {
            cmd |= CMD_DATA_STOP;
        }
        I2C_DATA_CMD(i2c) = cmd;

        I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x8u, timeout);
        if (timeout == 0 || I2C_CHECK_ABORT(i2c))
        {
            goto abort_exit;
        }

        data[i] = (uint8_t)(I2C_DATA_CMD(i2c) & 0xFFu);
    }

    I2C_WAIT_TIMEOUT(I2C_STATUS(i2c) & 0x4u, timeout);
    if (timeout == 0)
    {
        goto timeout_exit;
    }
    I2C_WAIT_TIMEOUT(I2C_RAW_INTR_STAT(i2c) & 0x200u, timeout);
    if (timeout == 0)
    {
        goto timeout_exit;
    }
    I2C_WAIT_TIMEOUT(!(I2C_STATUS(i2c) & 0x1u), timeout);
    if (timeout == 0)
    {
        goto timeout_exit;
    }

    set_i2c_state_clear(i2c);
    I2C_ENABLE(i2c) &= ~1u;
    i2c_record_success(i2c, true, dlen);
    return (int)dlen;

abort_exit:
    {
        uint32_t abrt_src = I2C_TX_ABRT_SOURCE(i2c);
        i2c_err_t err = I2C_ERR_ABORT;
        set_i2c_state_clear(i2c);
        I2C_ENABLE(i2c) &= ~1u;

        if (abrt_src & 0x0001u)
            err = I2C_ERR_NACK;
        if (abrt_src & 0x0002u)
            err = I2C_ERR_NACK;
        if (abrt_src & 0x1000u)
            err = I2C_ERR_ARB_LOST;

        i2c_record_error(i2c, err, abrt_src);
        if (err == I2C_ERR_ARB_LOST || err == I2C_ERR_ABORT)
        {
            (void)i2c_recover_bus(i2c);
        }
        return err;
    }

timeout_exit:
    set_i2c_state_clear(i2c);
    I2C_ENABLE(i2c) &= ~1u;
    i2c_record_error(i2c, I2C_ERR_TIMEOUT, 0);
    (void)i2c_recover_bus(i2c);
    return I2C_ERR_TIMEOUT;
}

