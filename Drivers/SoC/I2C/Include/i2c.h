/**
 * @file i2c.h
 * @brief S300 硬件 I2C 驱动头文件 (直接移植SDK)
 * 
 * 基于 DesignWare DW_apb_i2c 控制器
 */

#ifndef __S300_I2C_H__
#define __S300_I2C_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- I2C 基地址 -------------------- */

#define I2C_BASE                        (0x40014000u)

/* -------------------- I2C 寄存器宏 (SDK 风格) -------------------- */

#define I2C_CON(n)                      (*((volatile uint32_t*)(I2C_BASE + 0x0000 + ((n) * 0x1000))))
#define I2C_TAR(n)                      (*((volatile uint32_t*)(I2C_BASE + 0x0004 + ((n) * 0x1000))))
#define I2C_SAR(n)                      (*((volatile uint32_t*)(I2C_BASE + 0x0008 + ((n) * 0x1000))))
#define I2C_DATA_CMD(n)                 (*((volatile uint32_t*)(I2C_BASE + 0x0010 + ((n) * 0x1000))))
#define I2C_SS_SCL_HCNT(n)              (*((volatile uint32_t*)(I2C_BASE + 0x0014 + ((n) * 0x1000))))
#define I2C_SS_SCL_LCNT(n)              (*((volatile uint32_t*)(I2C_BASE + 0x0018 + ((n) * 0x1000))))
#define I2C_FS_SCL_HCNT(n)              (*((volatile uint32_t*)(I2C_BASE + 0x001C + ((n) * 0x1000))))
#define I2C_FS_SCL_LCNT(n)              (*((volatile uint32_t*)(I2C_BASE + 0x0020 + ((n) * 0x1000))))
#define I2C_HS_SCL_HCNT(n)              (*((volatile uint32_t*)(I2C_BASE + 0x0024 + ((n) * 0x1000))))
#define I2C_HS_SCL_LCNT(n)              (*((volatile uint32_t*)(I2C_BASE + 0x0028 + ((n) * 0x1000))))
#define I2C_INTR_STAT(n)                (*((volatile uint32_t*)(I2C_BASE + 0x002C + ((n) * 0x1000))))
#define I2C_INTR_MASK(n)                (*((volatile uint32_t*)(I2C_BASE + 0x0030 + ((n) * 0x1000))))
#define I2C_RAW_INTR_STAT(n)            (*((volatile uint32_t*)(I2C_BASE + 0x0034 + ((n) * 0x1000))))
#define I2C_RX_TL(n)                    (*((volatile uint32_t*)(I2C_BASE + 0x0038 + ((n) * 0x1000))))
#define I2C_TX_TL(n)                    (*((volatile uint32_t*)(I2C_BASE + 0x003C + ((n) * 0x1000))))
#define I2C_CLR_INTR(n)                 (*((volatile uint32_t*)(I2C_BASE + 0x0040 + ((n) * 0x1000))))
#define I2C_CLR_RX_UNDER(n)             (*((volatile uint32_t*)(I2C_BASE + 0x0044 + ((n) * 0x1000))))
#define I2C_CLR_RX_OVER(n)              (*((volatile uint32_t*)(I2C_BASE + 0x0048 + ((n) * 0x1000))))
#define I2C_CLR_TX_OVER(n)              (*((volatile uint32_t*)(I2C_BASE + 0x004C + ((n) * 0x1000))))
#define I2C_CLR_RD_REQ(n)               (*((volatile uint32_t*)(I2C_BASE + 0x0050 + ((n) * 0x1000))))
#define I2C_CLR_TX_ABRT(n)              (*((volatile uint32_t*)(I2C_BASE + 0x0054 + ((n) * 0x1000))))
#define I2C_CLR_RX_DONE(n)              (*((volatile uint32_t*)(I2C_BASE + 0x0058 + ((n) * 0x1000))))
#define I2C_CLR_ACTIVITY(n)             (*((volatile uint32_t*)(I2C_BASE + 0x005C + ((n) * 0x1000))))
#define I2C_CLR_STOP_DET(n)             (*((volatile uint32_t*)(I2C_BASE + 0x0060 + ((n) * 0x1000))))
#define I2C_CLR_START_DET(n)            (*((volatile uint32_t*)(I2C_BASE + 0x0064 + ((n) * 0x1000))))
#define I2C_CLR_GEN_CALL(n)             (*((volatile uint32_t*)(I2C_BASE + 0x0068 + ((n) * 0x1000))))
#define I2C_ENABLE(n)                   (*((volatile uint32_t*)(I2C_BASE + 0x006C + ((n) * 0x1000))))
#define I2C_STATUS(n)                   (*((volatile uint32_t*)(I2C_BASE + 0x0070 + ((n) * 0x1000))))
#define I2C_TXFLR(n)                    (*((volatile uint32_t*)(I2C_BASE + 0x0074 + ((n) * 0x1000))))
#define I2C_RXFLR(n)                    (*((volatile uint32_t*)(I2C_BASE + 0x0078 + ((n) * 0x1000))))
#define I2C_SDA_HOLD(n)                 (*((volatile uint32_t*)(I2C_BASE + 0x007C + ((n) * 0x1000))))
#define I2C_TX_ABRT_SOURCE(n)           (*((volatile uint32_t*)(I2C_BASE + 0x0080 + ((n) * 0x1000))))
#define I2C_SLV_DATA_NACK_ONLY(n)       (*((volatile uint32_t*)(I2C_BASE + 0x0084 + ((n) * 0x1000))))
#define I2C_DMA_CR(n)                   (*((volatile uint32_t*)(I2C_BASE + 0x0088 + ((n) * 0x1000))))
#define I2C_DMA_TDLR(n)                 (*((volatile uint32_t*)(I2C_BASE + 0x008C + ((n) * 0x1000))))
#define I2C_DMA_RDLR(n)                 (*((volatile uint32_t*)(I2C_BASE + 0x0090 + ((n) * 0x1000))))
#define I2C_SDA_SETUP(n)                (*((volatile uint32_t*)(I2C_BASE + 0x0094 + ((n) * 0x1000))))
#define I2C_ACK_GENERAL_CALL(n)         (*((volatile uint32_t*)(I2C_BASE + 0x0098 + ((n) * 0x1000))))
#define I2C_ENABLE_STATUS(n)            (*((volatile uint32_t*)(I2C_BASE + 0x009C + ((n) * 0x1000))))
#define I2C_FS_SPKLEN(n)                (*((volatile uint32_t*)(I2C_BASE + 0x00A0 + ((n) * 0x1000))))
#define I2C_HS_SPKLEN(n)                (*((volatile uint32_t*)(I2C_BASE + 0x00A4 + ((n) * 0x1000))))
#define I2C_CLR_RESTART_DET(n)          (*((volatile uint32_t*)(I2C_BASE + 0x00A8 + ((n) * 0x1000))))

/* -------------------- I2C DATA_CMD 标志 -------------------- */

#define CMD_DATA_READ                   (1u << 8)
#define CMD_DATA_STOP                   (1u << 9)
#define CMD_DATA_RESTART                (1u << 10)

/* -------------------- I2C 枚举类型 (SDK 风格) -------------------- */

/**
 * @brief I2C 控制器索引
 */
typedef enum {
    EM_I2C0 = 0,
    EM_I2C1 = 1,
    EM_I2C2 = 2,
    EM_I2C3 = 3,
} emI2C;

/**
 * @brief I2C 属性配置
 */
typedef enum {
    EM_I2C_SLAVE        = 0x00,     /**< 从机模式 */
    EM_I2C_MASTER       = 0x41,     /**< 主机模式 */
    EM_I2C_100K         = 0x02,     /**< 标准模式 (0-100 Kb/s) */
    EM_I2C_400K         = 0x04,     /**< 快速模式 (≤400 Kb/s) */
    EM_I2C_HIGH         = 0x06,     /**< 高速模式 (≤3.4 Mb/s) */
    EM_I2C_SLAVE_10BIT  = 0x08,     /**< 从机10位地址 */
    EM_I2C_MASTER_10BIT = 0x10,     /**< 主机10位地址 */
    EM_I2C_RESTART_EN   = 0x20,     /**< 使能RESTART条件 */
    EM_I2C_STOP_DEF     = 0x80,     /**< 仅被寻址时发STOP_DET中断 */
    EM_I2C_INTERRUPT    = 0x100,    /**< 使能中断 */
    EM_I2C_SPECIAL      = 0x800,    /**< 特殊I2C命令 IC_TAR@11 */
    EM_I2C_GC_OR_START  = 0x400,    /**< General Call或START字节 IC_TAR@10 */
} emI2CPRO;

/**
 * @brief I2C 中断类型
 */
typedef enum {
    EM_I2C_RX_UNDER     = 0x0001,
    EM_I2C_RX_OVER      = 0x0002,
    EM_I2C_RX_FULL      = 0x0004,
    EM_I2C_TX_OVER      = 0x0008,
    EM_I2C_TX_EPTY      = 0x0010,
    EM_I2C_RD_REQ       = 0x0020,
    EM_I2C_TX_ABRT      = 0x0040,
    EM_I2C_RX_DONE      = 0x0080,
    EM_I2C_ACIVITY      = 0x0100,
    EM_I2C_STOP_DET     = 0x0200,
    EM_I2C_START_DET    = 0x0400,
    EM_I2C_GEN_CALL     = 0x0800,
    EM_I2C_RESTART_DET  = 0x1000,
    EM_I2C_MST_ON_HOLD  = 0x2000,
} emI2CINT;

/**
 * @brief 布尔类型 (SDK兼容)
 */
typedef enum {
    EM_BOOL_FALSE = 0,
    EM_BOOL_TRUE  = 1,
} emBoolean;

/* -------------------- SDK API 声明 -------------------- */

/**
 * @brief 初始化 I2C
 * @param i2c I2C 控制器编号 (0-3)
 * @param pro 属性配置，可用 OR 组合
 * @param saddr 设备地址
 * @param apbclock APB 时钟频率
 * @param i2cclock I2C 时钟频率
 * @return 0
 */
int init_i2c(emI2C i2c, emI2CPRO pro, uint16_t saddr, uint32_t apbclock, uint32_t i2cclock);

/**
 * @brief I2C 写操作
 * @param i2c I2C 控制器编号
 * @param saddr 从设备地址
 * @param address 寄存器地址
 * @param is16bit 是否16位地址
 * @param data 数据缓冲区
 * @param dlen 数据长度
 * @return 写入的字节数
 */
int write_i2c(emI2C i2c, uint16_t saddr, uint16_t address, emBoolean is16bit, uint8_t *data, uint16_t dlen);

/**
 * @brief I2C 读操作
 * @param i2c I2C 控制器编号
 * @param saddr 从设备地址
 * @param address 寄存器地址
 * @param is16bit 是否16位地址
 * @param data 数据缓冲区
 * @param dlen 数据长度
 * @return 读取的字节数
 */
int read_i2c(emI2C i2c, uint16_t saddr, uint16_t address, emBoolean is16bit, uint8_t *data, uint16_t dlen);

/**
 * @brief 使能/禁用 I2C
 * @param i2c I2C 控制器编号
 * @param en EM_BOOL_TRUE=使能, EM_BOOL_FALSE=禁用
 */
void set_i2c_enable(emI2C i2c, emBoolean en);

/**
 * @brief 设置 I2C 中断
 * @param i2c I2C 控制器编号
 * @param value 中断位掩码
 */
void set_i2c_interrupt(emI2C i2c, emI2CINT value);

/**
 * @brief 发送 STOP 条件
 * @param i2c I2C 控制器编号
 */
void set_i2c_stop(emI2C i2c);

/**
 * @brief 清除 I2C 中断状态
 * @param i2c I2C 控制器编号
 */
void set_i2c_state_clear(emI2C i2c);

/**
 * @brief 打印 I2C 寄存器状态 (调试用)
 * @param i2c I2C 控制器编号
 */
void i2c_dump_regs(emI2C i2c);

/* -------------------- I2C 从机模式 API -------------------- */

/**
 * @brief I2C 从机中断回调函数类型
 * @param i2c I2C 控制器编号
 * @param status 中断状态 (I2C_INTR_STAT 的值)
 * @param user_data 用户数据指针
 * 
 * 回调函数内需要处理的关键中断：
 * - 0x04 (RX_FULL): 接收到数据，从 I2C_DATA_CMD 读取
 * - 0x20 (RD_REQ): 主机请求数据，向 I2C_DATA_CMD 写入，然后清除 I2C_CLR_RD_REQ
 * - 0x80 (RX_DONE): 接收完成，清除 I2C_CLR_RX_DONE
 * - 其他: 清除 START_DET, STOP_DET, RESTART_DET
 */
typedef void (*i2c_slave_callback_t)(emI2C i2c, uint32_t status, void *user_data);

/**
 * @brief I2C 从机配置结构体
 */
typedef struct {
    uint16_t slave_addr;        /**< 7位从机地址 (0x00-0x7F) */
    emI2CPRO speed;             /**< 速度: EM_I2C_100K 或 EM_I2C_400K */
    uint16_t intr_mask;         /**< 中断掩码，推荐值 0x06A4 (RX_FULL|RD_REQ|RX_DONE|STOP_DET|START_DET) */
    i2c_slave_callback_t callback;  /**< 中断回调函数 (可为 NULL，用户自己实现 IRQHandler) */
    void *user_data;            /**< 传递给回调的用户数据 */
} i2c_slave_config_t;

/**
 * @brief 初始化 I2C 从机模式
 * @param i2c I2C 控制器编号 (0-3)
 * @param config 从机配置
 * @return 0=成功
 * 
 * 使用示例：
 * @code
 * i2c_slave_config_t cfg = {
 *     .slave_addr = 0x03,
 *     .speed = EM_I2C_100K,
 *     .intr_mask = I2C_SLAVE_DEFAULT_INTR_MASK,
 *     .callback = NULL,  // 自己实现 I2C1_IRQHandler
 * };
 * i2c_slave_init(EM_I2C1, &cfg);
 * @endcode
 */
int i2c_slave_init(emI2C i2c, const i2c_slave_config_t *config);

/**
 * @brief 使能 I2C 从机中断 (包括 NVIC 和 S300 中断控制器)
 * @param i2c I2C 控制器编号
 * @param enable true=使能, false=禁用
 * 
 * 此函数配置两级中断：
 * 1. NVIC 使能 (标准 ARM)
 * 2. S300 INT_CTRL 使能 (芯片特有)
 */
void i2c_slave_irq_enable(emI2C i2c, bool enable);

/**
 * @brief I2C 从机发送数据 (在 RD_REQ 中断中调用)
 * @param i2c I2C 控制器编号
 * @param data 要发送的字节
 */
static inline void i2c_slave_send_byte(emI2C i2c, uint8_t data)
{
    I2C_DATA_CMD(i2c) = data;
}

/**
 * @brief I2C 从机读取数据 (在 RX_FULL 中断中调用)
 * @param i2c I2C 控制器编号
 * @return 接收到的字节
 */
static inline uint8_t i2c_slave_recv_byte(emI2C i2c)
{
    return (uint8_t)(I2C_DATA_CMD(i2c) & 0xFF);
}

/** 从机模式推荐的中断掩码 */
#define I2C_SLAVE_DEFAULT_INTR_MASK     (0x06A4u)
/* 包含: RX_FULL(0x04) | RD_REQ(0x20) | RX_DONE(0x80) | STOP_DET(0x200) | START_DET(0x400) */

/* -------------------- S300 中断控制器 -------------------- */

/**
 * @brief S300 中断控制器 (INT_CTRL) 定义
 * 
 * S300 有两级中断控制：
 * 1. 标准 ARM NVIC
 * 2. S300 特有的 INT_CTRL (0x40007000)，需要清除 mask 位才能使能中断
 */
#define S300_INT_CTRL_BASE              (0x40007000UL)
#define S300_ARM_INT_MASK(n)            (*((volatile uint32_t*)(S300_INT_CTRL_BASE + (n)*0x0004)))

/** I2C 中断号 (NVIC 和 INT_CTRL 共用) */
#define S300_IRQ_I2C0                   17
#define S300_IRQ_I2C1                   18
#define S300_IRQ_I2C2                   19
#define S300_IRQ_I2C3                   20

/* -------------------- BSP 兼容类型 -------------------- */

typedef emI2C       i2c_idx_t;
typedef emBoolean   i2c_bool_t;

#define I2C_IDX0    EM_I2C0
#define I2C_IDX1    EM_I2C1
#define I2C_IDX2    EM_I2C2
#define I2C_IDX3    EM_I2C3

#define I2C_SPEED_100K  100000u
#define I2C_SPEED_400K  400000u

typedef enum {
    I2C_OK          = 0,
    I2C_ERR_PARAM   = -1,
    I2C_ERR_TIMEOUT = -2,
    I2C_ERR_NACK    = -3,
    I2C_ERR_ARB_LOST= -4,
    I2C_ERR_ABORT   = -5,
} i2c_err_t;

/* -------------------- 超时配置 -------------------- */

/** 默认超时循环计数 (约 50ms @ 100MHz) */
#define I2C_DEFAULT_TIMEOUT     (5000000UL)

/** 设置全局超时值 */
void i2c_set_timeout(uint32_t timeout_loops);

/** 获取当前超时值 */
uint32_t i2c_get_timeout(void);

/* -------------------- 增强型 API (带超时和错误处理) -------------------- */

/**
 * @brief I2C 写操作 (带超时)
 * @param i2c I2C 控制器编号
 * @param saddr 从设备地址
 * @param address 寄存器地址
 * @param is16bit 是否16位地址
 * @param data 数据缓冲区
 * @param dlen 数据长度
 * @return >=0 写入的字节数, <0 错误码 (i2c_err_t)
 */
int i2c_write(emI2C i2c, uint16_t saddr, uint16_t address, emBoolean is16bit, uint8_t *data, uint16_t dlen);

/**
 * @brief I2C 读操作 (带超时)
 * @param i2c I2C 控制器编号
 * @param saddr 从设备地址
 * @param address 寄存器地址
 * @param is16bit 是否16位地址
 * @param data 数据缓冲区
 * @param dlen 数据长度
 * @return >=0 读取的字节数, <0 错误码 (i2c_err_t)
 */
int i2c_read(emI2C i2c, uint16_t saddr, uint16_t address, emBoolean is16bit, uint8_t *data, uint16_t dlen);

/**
 * @brief I2C 批量读操作（单次事务连续读取）
 * @param i2c I2C 控制器编号
 * @param saddr 从设备地址
 * @param address 寄存器地址
 * @param is16bit 是否16位地址
 * @param data 数据缓冲区
 * @param dlen 数据长度
 * @return >=0 读取的字节数, <0 错误码 (i2c_err_t)
 */
int i2c_read_burst(emI2C i2c, uint16_t saddr, uint16_t address, emBoolean is16bit, uint8_t *data, uint16_t dlen);

/**
 * @brief 检查 I2C 最后一次操作的传输中止原因
 * @param i2c I2C 控制器编号
 * @return TX_ABRT_SOURCE 寄存器值
 */
uint32_t i2c_get_abort_source(emI2C i2c);

/**
 * @brief 清除 I2C 中止状态
 * @param i2c I2C 控制器编号
 */
void i2c_clear_abort(emI2C i2c);

/**
 * @brief I2C 运行统计信息
 */
typedef struct {
    uint32_t tx_ok_count;       /**< 写成功次数 */
    uint32_t rx_ok_count;       /**< 读成功次数 */
    uint32_t tx_ok_bytes;       /**< 写成功字节数 */
    uint32_t rx_ok_bytes;       /**< 读成功字节数 */
    uint32_t timeout_count;     /**< 超时次数 */
    uint32_t nack_count;        /**< NACK 次数 */
    uint32_t arb_lost_count;    /**< 仲裁丢失次数 */
    uint32_t abort_count;       /**< 其他中止次数 */
    uint32_t recover_count;     /**< 恢复次数 */
    int32_t  last_error;        /**< 最后错误码 (i2c_err_t) */
    uint32_t last_abort_source; /**< 最后一次 TX_ABRT_SOURCE */
} i2c_stats_t;

/**
 * @brief 重置指定 I2C 的统计信息
 * @param i2c I2C 控制器编号
 */
void i2c_reset_stats(emI2C i2c);

/**
 * @brief 读取指定 I2C 的统计信息
 * @param i2c I2C 控制器编号
 * @param stats 输出统计结构体指针
 */
void i2c_get_stats(emI2C i2c, i2c_stats_t *stats);

/**
 * @brief I2C 软恢复（清状态并重新使能）
 * @param i2c I2C 控制器编号
 * @return 0=成功, <0=参数错误
 */
int i2c_recover_bus(emI2C i2c);

#ifdef __cplusplus
}
#endif

#endif /* __S300_I2C_H__ */
