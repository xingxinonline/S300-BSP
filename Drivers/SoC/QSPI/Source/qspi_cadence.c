#include "qspi_cadence.h"
#include "rcc.h"
#include "s300.h"
#include <string.h>
#include <stdio.h>

#define REG32(base, off) (*(volatile uint32_t *)((uintptr_t)(base) + (off)))

qspi_cadence_t g_qspi =
{
    .reg = (volatile uint8_t *)QSPI_CFG_BASE,
    .ahb = (volatile uint8_t *)M4_SLV_FLASH_BASE,
    .ahb_size = (uint32_t)(M4_SLV_FLASH_END - M4_SLV_FLASH_BASE + 1u),
    .ref_clk_hz = 0u,
    .sclk_hz = 0u,
    .fifo_depth = 128u,
    .fifo_width = 4u,
    .page_size = 256u,
    .block_4k_units = 16u /* 16 x 4KB = 64KB */
};

/* 当前 STIG 命令使用的地址字节数（3B/4B）在驱动内部跟踪，避免接口膨胀 */
static uint8_t s_addr_bytes = 3u;

static bool s_qspi_verbose = true;
void qspi_set_verbose(bool enable)
{
    s_qspi_verbose = enable;
}

/* Forward declaration for detailed mode dump */
static void qspi_dump_modes(const char *tag);

static inline void qspi_enable(bool en)
{
    uint32_t v = REG32(g_qspi.reg, CQSPI_REG_CONFIG);
    if (en) v |= CQSPI_CFG_ENABLE;
    else v &= ~CQSPI_CFG_ENABLE;
    REG32(g_qspi.reg, CQSPI_REG_CONFIG) = v;
}

static inline int qspi_wait_idle(void)
{
    /* poll CONFIG.IDLE bit */
    for (uint32_t t = 0; t < 1000000u; ++t)
    {
        if ((REG32(g_qspi.reg, CQSPI_REG_CONFIG) >> CQSPI_CFG_IDLE_LSB) & 1u)
            return 0;
    }
    return -1;
}

static void qspi_set_baud(uint32_t ref_hz, uint32_t sclk)
{
    if (sclk == 0u) sclk = ref_hz / 4u;
    /* div = ceil(ref / (2*sclk)) - 1, clamp to 0..0xF */
    uint32_t div = (ref_hz + (2u * sclk - 1u)) / (2u * sclk);
    if (div > 0) div -= 1u;
    if (div > CQSPI_CFG_BAUD_MASK) div = CQSPI_CFG_BAUD_MASK;
    uint32_t v = REG32(g_qspi.reg, CQSPI_REG_CONFIG);
    v &= ~(CQSPI_CFG_BAUD_MASK << CQSPI_CFG_BAUD_LSB);
    v |= (div << CQSPI_CFG_BAUD_LSB);
    REG32(g_qspi.reg, CQSPI_REG_CONFIG) = v;
}

static void qspi_set_mode_cpol0_cpha0(void)
{
    uint32_t v = REG32(g_qspi.reg, CQSPI_REG_CONFIG);
    v &= ~(CQSPI_CFG_CLK_POL | CQSPI_CFG_CLK_PHA);
    REG32(g_qspi.reg, CQSPI_REG_CONFIG) = v;
}

/* 统一使用公开的 qspi_exit_xip_mode()，该实现会同步清理 XIP_NEXT/IMM 与 MODE bits。 */

static void qspi_set_cs(unsigned cs)
{
    /* no decoder; hardware maps CS0->1110, but u-boot helper converts; we just program nibble directly. Use CS0. */
    (void)cs;
    uint32_t v = REG32(g_qspi.reg, CQSPI_REG_CONFIG);
    v &= ~(CQSPI_CFG_CHIPSELECT_MASK << CQSPI_CFG_CHIPSELECT_LSB);
    v |= ((0xEu) << CQSPI_CFG_CHIPSELECT_LSB); /* CS0 active */
    REG32(g_qspi.reg, CQSPI_REG_CONFIG) = v;
}

static int qspi_exec_cmd(uint32_t cmdctrl)
{
    REG32(g_qspi.reg, CQSPI_REG_CMDCTRL) = cmdctrl;
    REG32(g_qspi.reg, CQSPI_REG_CMDCTRL) = cmdctrl | CQSPI_CMDCTRL_EXECUTE;
    
    /* 根据频率调整超时时间 - 高频时需要更多时间 */
    uint32_t timeout = 1000000u;
    if (g_qspi.sclk_hz >= 80000000u) {
        timeout = 2000000u;  /* 高频时增加超时 */
    } else if (g_qspi.sclk_hz >= 50000000u) {
        timeout = 1500000u;  /* 中高频时适当增加 */
    }
    
    /* wait complete */
    uint32_t t;
    for (t = 0; t < timeout; ++t)
    {
        uint32_t r = REG32(g_qspi.reg, CQSPI_REG_CMDCTRL);
        if ((r & CQSPI_CMDCTRL_INPROGRESS) == 0u) break;
        
        /* 每1000次循环添加一个小延时，让硬件有时间响应 */
        if ((t % 1000u) == 999u) {
            for (volatile uint32_t i = 0; i < 10u; i++) __NOP();
        }
    }
    
    /* 检查是否超时 */
    if (t >= timeout) {
        if (s_qspi_verbose) {
            printf("[QSPI] Command timeout @%lu Hz\n", (unsigned long)g_qspi.sclk_hz);
        }
        /* 清除状态并返回错误 */
        REG32(g_qspi.reg, CQSPI_REG_CMDCTRL) = 0u;
        return -1;
    }
    
    /* clear */
    REG32(g_qspi.reg, CQSPI_REG_CMDCTRL) = 0u;
    return qspi_wait_idle();
}

static void qspi_readdata_capture(unsigned delay)
{
    uint32_t v = REG32(g_qspi.reg, CQSPI_REG_RD_DATA_CAPTURE);
    /* BYPASS=0 时 delay 字段才生效；delay=0 则可置 BYPASS=1 以旁路 */
    v &= ~(CQSPI_RD_CAPTURE_DELAY_MASK << CQSPI_RD_CAPTURE_DELAY_LSB);
    if (delay == 0u)
    {
        v |= CQSPI_RD_CAPTURE_BYPASS;
    }
    else
    {
        v &= ~CQSPI_RD_CAPTURE_BYPASS;
        v |= ((delay & CQSPI_RD_CAPTURE_DELAY_MASK) << CQSPI_RD_CAPTURE_DELAY_LSB);
    }
    REG32(g_qspi.reg, CQSPI_REG_RD_DATA_CAPTURE) = v;
}

/* ---- Timing helpers (keep logic centralized and readable) ---- */
typedef struct {
    uint32_t tshsl;
    uint32_t tchsh;
    uint32_t tslch;
    uint32_t tsd2d;
    uint32_t capture_delay;
} qspi_timing_cfg_t;

static inline void qspi_compute_timing_cfg(uint32_t sclk_hz, qspi_timing_cfg_t *out)
{
    /* 根据频率动态调整时序参数（阈值与原逻辑一致） */
    if (sclk_hz >= 96000000u)
    {
        /* 高频 (>=96MHz): 更保守的时序 */
        out->tshsl = 255u;
        out->tchsh = 50u;
        out->tslch = 50u;
        out->tsd2d = 255u;
        out->capture_delay = 3u;
    }
    else if (sclk_hz >= 48000000u)
    {
        /* 中高频 (48-96MHz): 适中时序 */
        out->tshsl = 200u;
        out->tchsh = 30u;
        out->tslch = 30u;
        out->tsd2d = 200u;
        out->capture_delay = 2u;
    }
    else if (sclk_hz >= 24000000u)
    {
        /* 中频 (24-48MHz): 标准时序 */
        out->tshsl = 150u;
        out->tchsh = 20u;
        out->tslch = 20u;
        out->tsd2d = 150u;
        out->capture_delay = 1u;
    }
    else
    {
        /* 低频 (<24MHz): 最小时序 */
        out->tshsl = 100u;
        out->tchsh = 10u;
        out->tslch = 10u;
        out->tsd2d = 100u;
        out->capture_delay = 0u; /* 可旁路（见后续特例修正） */
    }
}

static inline void qspi_apply_delay_regs(const qspi_timing_cfg_t *cfg)
{
    REG32(g_qspi.reg, CQSPI_REG_DELAY) =
        (cfg->tshsl << CQSPI_DELAY_TSHSL_LSB) |
        (cfg->tchsh << CQSPI_DELAY_TCHSH_LSB) |
        (cfg->tslch << CQSPI_DELAY_TSLCH_LSB) |
        (cfg->tsd2d << CQSPI_DELAY_TSD2D_LSB);
    qspi_readdata_capture(cfg->capture_delay);
}

static inline bool qspi_is_low_ref_half_rate(uint32_t ref_clk_hz, uint32_t sclk_hz)
{
    if (ref_clk_hz > 24000000u) return false;
    uint32_t half = ref_clk_hz / 2u;
    return (sclk_hz >= half);
}

static inline void qspi_apply_low_ref_half_rate_adjustment(void)
{
    /*
     * 在低参考时钟（如 24MHz）且目标 SCLK 接近 ref/2（即分频=0）场景下，
     * 提高采样稳定性：
     * - 至少使用 2 个采样延时（关闭 BYPASS）
     * - 切换采样沿（SAMPLE_EDGE=1）
     * - 发送方向增加 1 个 TX 延时
     */
    uint32_t cap = REG32(g_qspi.reg, CQSPI_REG_RD_DATA_CAPTURE);
    cap |= CQSPI_RD_CAPTURE_SAMPLE_EDGE;
    cap &= ~(CQSPI_RD_CAPTURE_TX_DELAY_MASK << CQSPI_RD_CAPTURE_TX_DELAY_LSB);
    cap |= (1u << CQSPI_RD_CAPTURE_TX_DELAY_LSB);
    cap &= ~(CQSPI_RD_CAPTURE_DELAY_MASK << CQSPI_RD_CAPTURE_DELAY_LSB);
    cap |= ((2u & CQSPI_RD_CAPTURE_DELAY_MASK) << CQSPI_RD_CAPTURE_DELAY_LSB);
    cap &= ~CQSPI_RD_CAPTURE_BYPASS;
    REG32(g_qspi.reg, CQSPI_REG_RD_DATA_CAPTURE) = cap;
}

static inline int qspi_wait_stig_done(uint32_t timeout)
{
    for (uint32_t t = 0; t < timeout; ++t)
    {
        uint32_t r = REG32(g_qspi.reg, CQSPI_REG_CMDCTRL);
        if ((r & CQSPI_CMDCTRL_INPROGRESS) == 0u) return 0;
        if ((t % 1000u) == 999u) { for (volatile uint32_t i = 0; i < 10u; ++i) __NOP(); }
    }
    return -1;
}

int qspi_stig_read_ex(uint8_t opcode, uint32_t addr, unsigned addr_bytes,
                      unsigned dummy_cycles, void *rx, uint32_t rx_len)
{
    if (!rx || rx_len == 0u) return -1;
    if (addr_bytes > 4u) return -1;
    if (qspi_wait_idle() != 0) return -1;

    /* 详细打印 STIG READ 参数与路径选择（<=8B 直读 / MemoryBank） */
    printf("s300_qspi: STIG READ op=0x%02X addr=0x%08lX addr_bytes=%u dummy=%u len=%lu\n",
           (unsigned)opcode, (unsigned long)addr, (unsigned)addr_bytes, (unsigned)dummy_cycles, (unsigned long)rx_len);

    /* If need >8B, use STIG memory bank */
    uint8_t *dst = (uint8_t *)rx;
    uint32_t remain = rx_len;

    if (remain <= 8u)
    {
        printf("s300_qspi: STIG READ path=direct (<=8B)\n");
        REG32(g_qspi.reg, CQSPI_REG_CMDADDRESS) = addr;
        uint32_t cmd = ((uint32_t)opcode << CQSPI_CMDCTRL_OPCODE_LSB) |
                       ((addr_bytes ? 1u : 0u) << CQSPI_CMDCTRL_ADDR_EN_LSB) |
                       (((addr_bytes ? addr_bytes : 0u) & CQSPI_CMDCTRL_ADD_BYTES_MASK) << CQSPI_CMDCTRL_ADD_BYTES_LSB) |
                       ((dummy_cycles & CQSPI_CMDCTRL_DUMMY_MASK) << CQSPI_CMDCTRL_DUMMY_LSB) |
                       (1u << CQSPI_CMDCTRL_RD_EN_LSB) |
                       ((((remain ? remain : 1u) - 1u) & CQSPI_CMDCTRL_RD_BYTES_MASK) << CQSPI_CMDCTRL_RD_BYTES_LSB);
        int rc = qspi_exec_cmd(cmd);
        if (rc) return rc;
        uint32_t low = REG32(g_qspi.reg, CQSPI_REG_CMDREADDATALOWER);
        uint32_t take = (remain > 4u) ? 4u : remain;
        memcpy(dst, &low, take);
        if (remain > 4u)
        {
            uint32_t up = REG32(g_qspi.reg, CQSPI_REG_CMDREADDATAUPPER);
            memcpy(dst + 4u, &up, remain - 4u);
        }
        return 0;
    }

    /* Use Memory Bank */
    printf("s300_qspi: STIG READ path=mem-bank (len=%lu bank_max=%u)\n",
        (unsigned long)remain, (unsigned)CQSPI_STIG_MEM_BANK_MAX_BYTES);
    REG32(g_qspi.reg, CQSPI_REG_CMDADDRESS) = addr;
    uint32_t cmd = ((uint32_t)opcode << CQSPI_CMDCTRL_OPCODE_LSB) |
                   ((addr_bytes ? 1u : 0u) << CQSPI_CMDCTRL_ADDR_EN_LSB) |
                   (((addr_bytes ? addr_bytes : 0u) & CQSPI_CMDCTRL_ADD_BYTES_MASK) << CQSPI_CMDCTRL_ADD_BYTES_LSB) |
                   ((dummy_cycles & CQSPI_CMDCTRL_DUMMY_MASK) << CQSPI_CMDCTRL_DUMMY_LSB) |
                   (1u << CQSPI_CMDCTRL_RD_EN_LSB) |
                   (1u << CQSPI_CMDCTRL_MEM_BANK_EN) |
                   ((((8u - 1u)) & CQSPI_CMDCTRL_RD_BYTES_MASK) << CQSPI_CMDCTRL_RD_BYTES_LSB);
    /* Trigger STIG with memory bank enabled */
    REG32(g_qspi.reg, CQSPI_REG_CMDCTRL) = cmd;
    REG32(g_qspi.reg, CQSPI_REG_CMDCTRL) = cmd | CQSPI_CMDCTRL_EXECUTE;
    if (qspi_wait_stig_done(2000000u) != 0) return -1;

    /* Read last 8 bytes directly */
    uint32_t total = remain;
    if (total >= 8u) {
        uint32_t last8_off = total - 8u;
        uint32_t low = REG32(g_qspi.reg, CQSPI_REG_CMDREADDATALOWER);
        uint32_t up  = REG32(g_qspi.reg, CQSPI_REG_CMDREADDATAUPPER);
        memcpy(dst + last8_off, &low, 4u);
        memcpy(dst + last8_off + 4u, &up, 4u);
    }

    /* If need more than last 8 bytes, fetch from memory bank by index */
    if (total > 8u)
    {
        uint32_t bank_max = CQSPI_STIG_MEM_BANK_MAX_BYTES;
        uint32_t fetch = (total < bank_max) ? total : bank_max; /* 若超过深度将环回覆盖，按规范只能保证前bank_max字节 */
        for (uint32_t i = 0; i < fetch; ++i)
        {
            uint32_t mem = 0u;
            mem |= ((i & CQSPI_FLASH_CMD_MEM_ADDR_MASK) << CQSPI_FLASH_CMD_MEM_ADDR_LSB);
            mem |= ((0u & CQSPI_FLASH_CMD_MEM_NUM_BYTES_MASK) << CQSPI_FLASH_CMD_MEM_NUM_BYTES_LSB); /* 单字节 */
            REG32(g_qspi.reg, CQSPI_REG_FLASH_CMD_CTRL_MEM) = mem;
            REG32(g_qspi.reg, CQSPI_REG_FLASH_CMD_CTRL_MEM) = mem | CQSPI_FLASH_CMD_MEM_TRIGGER;
            /* 等待该字节就绪 */
            for (uint32_t t = 0; t < 100000u; ++t)
            {
                uint32_t st = REG32(g_qspi.reg, CQSPI_REG_FLASH_CMD_CTRL_MEM);
                if ((st & CQSPI_FLASH_CMD_MEM_IN_PROGRESS) == 0u)
                {
                    uint32_t bytev = (st >> CQSPI_FLASH_CMD_MEM_DATA_LSB) & CQSPI_FLASH_CMD_MEM_DATA_MASK;
                    dst[i] = (uint8_t)bytev;
                    break;
                }
                if ((t % 1000u) == 999u) { for (volatile uint32_t k = 0; k < 10u; ++k) __NOP(); }
            }
        }
    }
    return 0;
}

int qspi_stig_write_ex(uint8_t opcode, uint32_t addr, unsigned addr_bytes,
                       unsigned dummy_cycles, const void *tx, uint32_t tx_len)
{
    if (tx_len > 8u) return -1; /* STIG写最多8字节 */
    if (addr_bytes > 4u) return -1;
    if (tx_len && !tx) return -1;
    if (qspi_wait_idle() != 0) return -1;

    printf("s300_qspi: STIG WRITE op=0x%02X addr=0x%08lX addr_bytes=%u dummy=%u len=%lu\n",
           (unsigned)opcode, (unsigned long)addr, (unsigned)addr_bytes, (unsigned)dummy_cycles, (unsigned long)tx_len);

    uint32_t lower = 0u, upper = 0u;
    if (tx_len)
    {
        memcpy(&lower, tx, (tx_len > 4u) ? 4u : tx_len);
        if (tx_len > 4u)
            memcpy(&upper, ((const uint8_t *)tx) + 4u, tx_len - 4u);
    }
    REG32(g_qspi.reg, CQSPI_REG_CMDWRITEDATALOWER) = lower;
    REG32(g_qspi.reg, CQSPI_REG_CMDWRITEDATAUPPER) = upper;
    REG32(g_qspi.reg, CQSPI_REG_CMDADDRESS) = addr;

    uint32_t cmd = ((uint32_t)opcode << CQSPI_CMDCTRL_OPCODE_LSB) |
                   ((addr_bytes ? 1u : 0u) << CQSPI_CMDCTRL_ADDR_EN_LSB) |
                   (((addr_bytes ? addr_bytes : 0u) & CQSPI_CMDCTRL_ADD_BYTES_MASK) << CQSPI_CMDCTRL_ADD_BYTES_LSB) |
                   ((dummy_cycles & CQSPI_CMDCTRL_DUMMY_MASK) << CQSPI_CMDCTRL_DUMMY_LSB) |
                   ((tx_len ? 1u : 0u) << CQSPI_CMDCTRL_WR_EN_LSB) |
                   ((((tx_len ? tx_len : 1u) - 1u) & CQSPI_CMDCTRL_WR_BYTES_MASK) << CQSPI_CMDCTRL_WR_BYTES_LSB);

    int rc = qspi_exec_cmd(cmd);
    return rc;
}

void qspi_cadence_init(uint32_t ref_clk_hz, uint32_t sclk_hz)
{
    /* Enable RCC clocks for QSPI on APB0 and AHB */
    rcc_set_cortex_m4_apb0_clock(RCC_CM4_APB0_QSPIFLASH, true);
    rcc_set_cortex_m4_ahb_clock(RCC_CM4_AHB_QSPIFLASH, true);
    rcc_set_cortex_m4_apb0_reset(RCC_CM4_APB0_QSPIFLASH, false);
    rcc_set_cortex_m4_ahb_reset(RCC_CM4_AHB_QSPIFLASH, false);
    g_qspi.ref_clk_hz = ref_clk_hz;
    g_qspi.sclk_hz = sclk_hz ? sclk_hz : (ref_clk_hz / 4u);
    qspi_enable(false);
    /* config basic: mode 0, CS0, baud, disable decode */
    uint32_t cfg = 0u;
    cfg |= (0xEu << CQSPI_CFG_CHIPSELECT_LSB); /* CS0 */
    REG32(g_qspi.reg, CQSPI_REG_CONFIG) = cfg;
    qspi_set_mode_cpol0_cpha0();
    qspi_set_baud(ref_clk_hz, g_qspi.sclk_hz);
    qspi_set_cs(0);
    /* size: 24-bit addr default, page 256, block 64KB */
    s_addr_bytes = 3u;
    uint32_t size = 0u;
    size |= (((uint32_t)s_addr_bytes - 1u) & CQSPI_SIZE_ADDR_MASK) << CQSPI_SIZE_ADDR_LSB; /* 3-bytes addr default */
    size |= (g_qspi.page_size << CQSPI_SIZE_PAGE_LSB);
    size |= (g_qspi.block_4k_units << CQSPI_SIZE_BLOCK_LSB);
    REG32(g_qspi.reg, CQSPI_REG_SIZE) = size;
    /* 设置 REMAP 为 AHB 窗口基址，使 CPU AHB 地址与控制器匹配 */
    REG32(g_qspi.reg, CQSPI_REG_REMAP) = (uint32_t)(uintptr_t)g_qspi.ahb;
    /* Default: half of total locations for indirect read, half for write.
       Avoid 0 and max per spec (only low 8 bits in fill-level readable). */
    uint32_t default_read_reg = (1u << (CQSPI_SRAM_DEPTH_N - 1u)); /* 0x80 for N=8 */
    REG32(g_qspi.reg, CQSPI_REG_SRAMPARTITION) = default_read_reg & CQSPI_SRAM_PARTITION_MASK;
    REG32(g_qspi.reg, CQSPI_REG_IRQMASK) = 0u;
    /* ensure we are not in XIP/direct mode left by bootrom (清除 XIP_NEXT/IMM、MODE bits) */
    qspi_exit_xip_mode();
    /* 清理可能由BootROM遗留的扩展指令配置，避免影响 STIG 读写（如 RDID 异常） */
    REG32(g_qspi.reg, CQSPI_REG_OPCODE_EXT_LOWER) = 0u;
    REG32(g_qspi.reg, CQSPI_REG_OPCODE_EXT_UPPER) = 0u;
    
    /* 1) 计算并应用基础时序 */
    qspi_timing_cfg_t tc;
    qspi_compute_timing_cfg(g_qspi.sclk_hz, &tc);
    /* 若低参考频率且接近 ref/2，至少确保 capture_delay>=1（关闭 BYPASS） */
    if (qspi_is_low_ref_half_rate(ref_clk_hz, g_qspi.sclk_hz) && tc.capture_delay < 1u)
        tc.capture_delay = 1u;
    qspi_apply_delay_regs(&tc);

    /* 2) 低参考频率 + ref/2 档位的额外稳健性增强（采样沿/延时/TX_DELAY） */
    if (qspi_is_low_ref_half_rate(ref_clk_hz, g_qspi.sclk_hz))
        qspi_apply_low_ref_half_rate_adjustment();
    /* instruction bus widths single-single-single */
    uint32_t rd = (CQSPI_INST_TYPE_SINGLE << CQSPI_RD_TYPE_INSTR_LSB) |
                  (CQSPI_INST_TYPE_SINGLE << CQSPI_RD_TYPE_ADDR_LSB)  |
                  (CQSPI_INST_TYPE_SINGLE << CQSPI_RD_TYPE_DATA_LSB);
    REG32(g_qspi.reg, CQSPI_REG_RD_INSTR) = rd;
    uint32_t wr = (CQSPI_INST_TYPE_SINGLE << CQSPI_WR_TYPE_ADDR_LSB) |
                  (CQSPI_INST_TYPE_SINGLE << CQSPI_WR_TYPE_DATA_LSB);
    REG32(g_qspi.reg, CQSPI_REG_WR_INSTR) = wr;
    /* 只使用STIG模式，不使用 DIRECT */
    cfg = REG32(g_qspi.reg, CQSPI_REG_CONFIG);
    cfg &= ~CQSPI_CFG_DIRECT;  /* 确保关闭 DIRECT 模式 */
    REG32(g_qspi.reg, CQSPI_REG_CONFIG) = cfg;
    qspi_enable(true);
}

uint32_t qspi_get_baud_raw(void)
{
    /* 读取 CONFIG 寄存器 BAUD 字段（4bit），编码为 raw=div，SCLK=ref/(2*(raw+1)) */
    uint32_t cfg = REG32(g_qspi.reg, CQSPI_REG_CONFIG);
    uint32_t raw = (cfg >> CQSPI_CFG_BAUD_LSB) & CQSPI_CFG_BAUD_MASK;
    return raw;
}

uint32_t qspi_get_actual_sclk_hz(void)
{
    uint32_t raw = qspi_get_baud_raw();
    uint32_t denom = 2u * (raw + 1u);
    if (denom == 0u) denom = 2u;
    if (g_qspi.ref_clk_hz == 0u) return 0u;
    return g_qspi.ref_clk_hz / denom;
}

int qspi_read_id(uint8_t *id, uint32_t len)
{
    if (!id || len == 0u) return -1;
    qspi_enable(true);
    /* STIG read command */
    uint32_t cmd = (W25Q_CMD_RDID << CQSPI_CMDCTRL_OPCODE_LSB) | (1u << CQSPI_CMDCTRL_RD_EN_LSB) |
                   (((len - 1u) & CQSPI_CMDCTRL_RD_BYTES_MASK) << CQSPI_CMDCTRL_RD_BYTES_LSB);
    int rc = qspi_exec_cmd(cmd);
    if (rc) return rc;
    uint32_t low = REG32(g_qspi.reg, CQSPI_REG_CMDREADDATALOWER);
    uint32_t copy = len > 4u ? 4u : len;
    memcpy(id, &low, copy);
    if (len > 4u)
    {
        uint32_t up = REG32(g_qspi.reg, CQSPI_REG_CMDREADDATAUPPER);
        memcpy(id + 4u, &up, len - 4u);
    }
    return 0;
}

int qspi_read_device_id(uint8_t *dev_id)
{
    if (!dev_id) return -1;
    qspi_enable(true);
    /* Device ID command (ABh) with 3 dummy bytes */
    REG32(g_qspi.reg, CQSPI_REG_CMDADDRESS) = 0x000000u; /* 3 dummy bytes */
    uint32_t cmd = (W25Q_CMD_DEVID << CQSPI_CMDCTRL_OPCODE_LSB) |
                   (1u << CQSPI_CMDCTRL_ADDR_EN_LSB) |
                   (((3u - 1u) & CQSPI_CMDCTRL_ADD_BYTES_MASK) << CQSPI_CMDCTRL_ADD_BYTES_LSB) |
                   (1u << CQSPI_CMDCTRL_RD_EN_LSB) |
                   (0u << CQSPI_CMDCTRL_RD_BYTES_LSB); /* 1 byte */
    int rc = qspi_exec_cmd(cmd);
    if (rc) return rc;
    uint32_t low = REG32(g_qspi.reg, CQSPI_REG_CMDREADDATALOWER);
    *dev_id = (uint8_t)(low & 0xFFu);
    return 0;
}

int qspi_read_manufacturer_device_id(uint8_t *mfg_id, uint8_t *dev_id)
{
    if (!mfg_id || !dev_id) return -1;
    qspi_enable(true);
    /* Manufacturer/Device ID command (90h) with address 000000h */
    REG32(g_qspi.reg, CQSPI_REG_CMDADDRESS) = 0x000000u;
    uint32_t cmd = (W25Q_CMD_MANDEV << CQSPI_CMDCTRL_OPCODE_LSB) |
                   (1u << CQSPI_CMDCTRL_ADDR_EN_LSB) |
                   (((3u - 1u) & CQSPI_CMDCTRL_ADD_BYTES_MASK) << CQSPI_CMDCTRL_ADD_BYTES_LSB) |
                   (1u << CQSPI_CMDCTRL_RD_EN_LSB) |
                   (((2u - 1u) & CQSPI_CMDCTRL_RD_BYTES_MASK) << CQSPI_CMDCTRL_RD_BYTES_LSB); /* 2 bytes */
    int rc = qspi_exec_cmd(cmd);
    if (rc) return rc;
    uint32_t low = REG32(g_qspi.reg, CQSPI_REG_CMDREADDATALOWER);
    *mfg_id = (uint8_t)(low & 0xFFu);
    *dev_id = (uint8_t)((low >> 8) & 0xFFu);
    return 0;
}

int qspi_read_unique_id(uint8_t *uid, uint32_t len)
{
    if (!uid || len == 0u) return -1;
    qspi_enable(true);
    /* Read Unique ID command (4Bh) with 4 dummy bytes */
    REG32(g_qspi.reg, CQSPI_REG_CMDADDRESS) = 0x00000000u; /* 4 dummy bytes */
    uint32_t read_len = (len > 8u) ? 8u : len; /* Max 8 bytes per STIG */
    uint32_t cmd = (W25Q_CMD_UNIQUE << CQSPI_CMDCTRL_OPCODE_LSB) |
                   (1u << CQSPI_CMDCTRL_ADDR_EN_LSB) |
                   (((4u - 1u) & CQSPI_CMDCTRL_ADD_BYTES_MASK) << CQSPI_CMDCTRL_ADD_BYTES_LSB) |
                   (1u << CQSPI_CMDCTRL_RD_EN_LSB) |
                   (((read_len - 1u) & CQSPI_CMDCTRL_RD_BYTES_MASK) << CQSPI_CMDCTRL_RD_BYTES_LSB);
    int rc = qspi_exec_cmd(cmd);
    if (rc) return rc;
    uint32_t low = REG32(g_qspi.reg, CQSPI_REG_CMDREADDATALOWER);
    uint32_t copy = read_len > 4u ? 4u : read_len;
    memcpy(uid, &low, copy);
    if (read_len > 4u)
    {
        uint32_t up = REG32(g_qspi.reg, CQSPI_REG_CMDREADDATAUPPER);
        memcpy(uid + 4u, &up, read_len - 4u);
    }
    return 0;
}

int qspi_read_sfdp(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!buf || len == 0u) return -1;
    qspi_enable(true);
    
    uint8_t *p = buf;
    uint32_t remaining = len;
    uint32_t current_addr = addr;
    
    while (remaining > 0u)
    {
        uint32_t chunk = (remaining > 8u) ? 8u : remaining;
        REG32(g_qspi.reg, CQSPI_REG_CMDADDRESS) = current_addr;
        uint32_t cmd = (W25Q_CMD_SFDP << CQSPI_CMDCTRL_OPCODE_LSB) |
                       (1u << CQSPI_CMDCTRL_ADDR_EN_LSB) |
                       (((3u - 1u) & CQSPI_CMDCTRL_ADD_BYTES_MASK) << CQSPI_CMDCTRL_ADD_BYTES_LSB) |
                       (1u << CQSPI_CMDCTRL_RD_EN_LSB) |
                       (((chunk - 1u) & CQSPI_CMDCTRL_RD_BYTES_MASK) << CQSPI_CMDCTRL_RD_BYTES_LSB) |
                       (8u << CQSPI_CMDCTRL_DUMMY_LSB); /* SFDP needs 8 dummy cycles */
        
        int rc = qspi_exec_cmd(cmd);
        if (rc) return rc;
        
        uint32_t low = REG32(g_qspi.reg, CQSPI_REG_CMDREADDATALOWER);
        uint32_t copy = chunk > 4u ? 4u : chunk;
        memcpy(p, &low, copy);
        if (chunk > 4u)
        {
            uint32_t up = REG32(g_qspi.reg, CQSPI_REG_CMDREADDATAUPPER);
            memcpy(p + 4u, &up, chunk - 4u);
        }
        
        p += chunk;
        current_addr += chunk;
        remaining -= chunk;
    }
    return 0;
}

static int qspi_wren(void)
{
    uint32_t cmd = (W25Q_CMD_WREN << CQSPI_CMDCTRL_OPCODE_LSB);
    int rc = qspi_exec_cmd(cmd);
    if (rc) return rc;
    
    /* 在高频下，WREN命令可能需要额外的时间才能生效 */
    /* 添加一个小延时以确保命令完全执行 */
    if (g_qspi.sclk_hz >= 80000000u) {
        /* 高频时增加延时 */
        for (volatile uint32_t i = 0; i < 100u; i++) __NOP();
    } else if (g_qspi.sclk_hz >= 50000000u) {
        /* 中高频时适量延时 */
        for (volatile uint32_t i = 0; i < 50u; i++) __NOP();
    }
    
    /* 验证WEL位是否已设置 */
    uint8_t sr1 = 0;
    rc = qspi_read_status(&sr1, NULL, NULL);
    if (rc) return rc;
    
    if ((sr1 & 0x02u) == 0) {
        /* WEL位未设置，可能是时序问题，再试一次 */
        rc = qspi_exec_cmd(cmd);
        if (rc) return rc;
        
        /* 再次添加延时 */
        if (g_qspi.sclk_hz >= 50000000u) {
            for (volatile uint32_t i = 0; i < 200u; i++) __NOP();
        }
        
        /* 再次验证 */
        rc = qspi_read_status(&sr1, NULL, NULL);
        if (rc) return rc;
        
        if ((sr1 & 0x02u) == 0) {
            /* 仍然失败，返回错误 */
            return -1;
        }
    }
    
    return 0;
}

static int qspi_write_sr12(uint8_t sr1, uint8_t sr2)
{
    int rc = qspi_wren();
    if (rc) return rc;
    REG32(g_qspi.reg, CQSPI_REG_CMDWRITEDATALOWER) = ((uint32_t)sr1) | (((uint32_t)sr2) << 8);
    uint32_t cmd = (W25Q_CMD_WRSR12 << CQSPI_CMDCTRL_OPCODE_LSB) |
                   (1u << CQSPI_CMDCTRL_WR_EN_LSB) |
                   (((2u - 1u) & CQSPI_CMDCTRL_WR_BYTES_MASK) << CQSPI_CMDCTRL_WR_BYTES_LSB);
    rc = qspi_exec_cmd(cmd);
    if (rc) return rc;
    return qspi_wait_ready(10u);
}

int qspi_unlock_all(void)
{
    uint8_t s1 = 0, s2 = 0, s3 = 0;
    int rc = qspi_read_status(&s1, &s2, &s3);
    if (rc) return rc;
    if (s_qspi_verbose)
        printf("[QSPI] Before unlock SR1=%02X SR2=%02X SR3=%02X\n", s1, s2, s3);
    /* Clear SR1 block protect bits BP[2:0]=0, TB=0, SRP0(SRWD)=0; preserve rest */
    uint8_t new1 = s1 & ~((uint8_t)0x3Cu /* BP2:4 + TB */ | (uint8_t)0x80u /* SRP0 */);
    uint8_t new2 = s2 & ~((uint8_t)0x40u /* SRP1 */ | (uint8_t)0x38u /* BP[5:3] */);
    /* Keep QE (bit1 of SR2) as-is. */
    rc = qspi_write_sr12(new1, new2);
    if (rc) return rc;
    /* SR3: typically holds drive strength/latency, no lock bits; keep as-is */
    rc = qspi_read_status(&s1, &s2, &s3);
    if (s_qspi_verbose)
        printf("[QSPI] After unlock SR1=%02X SR2=%02X SR3=%02X\n", s1, s2, s3);
    return rc;
}

int qspi_wait_ready(uint32_t timeout_ms)
{
    uint32_t loops = timeout_ms * 1000u; /* approx 1us polls */
    while (loops--)
    {
        uint32_t cmd = (W25Q_CMD_RDSR1 << CQSPI_CMDCTRL_OPCODE_LSB) |
                       (1u << CQSPI_CMDCTRL_RD_EN_LSB) |
                       (0u << CQSPI_CMDCTRL_RD_BYTES_LSB);
        int rc = qspi_exec_cmd(cmd);
        if (rc) return rc;
        uint32_t v = REG32(g_qspi.reg, CQSPI_REG_CMDREADDATALOWER) & 0xFFu;
        if ((v & 0x01u) == 0u) return 0; /* WIP=0 ready */
        for (volatile uint32_t d = 0; d < 100u; ++d) __NOP();
    }
    return -1;
}

int qspi_erase_4k(uint32_t addr)
{
    int rc = qspi_wren();
    if (rc) return rc;
    REG32(g_qspi.reg, CQSPI_REG_CMDADDRESS) = addr;
    uint32_t cmd = (W25Q_CMD_SE_4K << CQSPI_CMDCTRL_OPCODE_LSB) |
                   (1u << CQSPI_CMDCTRL_ADDR_EN_LSB) |
                   ((((uint32_t)s_addr_bytes - 1u) & CQSPI_CMDCTRL_ADD_BYTES_MASK) << CQSPI_CMDCTRL_ADD_BYTES_LSB);
    rc = qspi_exec_cmd(cmd);
    if (rc) return rc;
    return qspi_wait_ready(4000u);
}

int qspi_erase_64k(uint32_t addr)
{
    int rc = qspi_wren();
    if (rc) return rc;
    REG32(g_qspi.reg, CQSPI_REG_CMDADDRESS) = addr;
    uint32_t cmd = (W25Q_CMD_BE_64K << CQSPI_CMDCTRL_OPCODE_LSB) |
                   (1u << CQSPI_CMDCTRL_ADDR_EN_LSB) |
                   ((((uint32_t)s_addr_bytes - 1u) & CQSPI_CMDCTRL_ADD_BYTES_MASK) << CQSPI_CMDCTRL_ADD_BYTES_LSB);
    rc = qspi_exec_cmd(cmd);
    if (rc) return rc;
    return qspi_wait_ready(8000u);
}

int qspi_erase_32k(uint32_t addr)
{
    int rc = qspi_wren();
    if (rc) return rc;
    REG32(g_qspi.reg, CQSPI_REG_CMDADDRESS) = addr;
    uint32_t cmd = (W25Q_CMD_BE_32K << CQSPI_CMDCTRL_OPCODE_LSB) |
                   (1u << CQSPI_CMDCTRL_ADDR_EN_LSB) |
                   ((((uint32_t)s_addr_bytes - 1u) & CQSPI_CMDCTRL_ADD_BYTES_MASK) << CQSPI_CMDCTRL_ADD_BYTES_LSB);
    rc = qspi_exec_cmd(cmd);
    if (rc) return rc;
    return qspi_wait_ready(6000u); /* 32KB erase timeout */
}

int qspi_chip_erase(void)
{
    int rc = qspi_wren();
    if (rc) return rc;
    uint32_t cmd = (W25Q_CMD_CE << CQSPI_CMDCTRL_OPCODE_LSB);
    rc = qspi_exec_cmd(cmd);
    if (rc) return rc;
    return qspi_wait_ready(200000u); /* up to seconds */
}

int qspi_reset_enable(void)
{
    uint32_t cmd = (W25Q_CMD_RSTEN << CQSPI_CMDCTRL_OPCODE_LSB);
    return qspi_exec_cmd(cmd);
}

int qspi_reset_device(void)
{
    uint32_t cmd = (W25Q_CMD_RST << CQSPI_CMDCTRL_OPCODE_LSB);
    return qspi_exec_cmd(cmd);
}

int qspi_software_reset(void)
{
    /* 软件复位序列：先使能复位，再执行复位 */
    int rc = qspi_reset_enable();
    if (rc) return rc;
    
    /* 短暂延时确保使能命令生效 */
    for (volatile uint32_t i = 0; i < 1000u; ++i) __NOP();
    
    rc = qspi_reset_device();
    if (rc) return rc;
    
    /* 复位后延时，等待设备重新初始化 */
    for (volatile uint32_t i = 0; i < 10000u; ++i) __NOP();
    
    if (s_qspi_verbose)
        printf("[QSPI] Software reset completed\n");
    
    return 0;
}

int qspi_set_quad_enable(bool enable)
{
    uint8_t sr1 = 0, sr2 = 0;
    int rc = qspi_read_status(&sr1, &sr2, NULL);
    if (rc) return rc;
    uint8_t new2 = sr2;
    if (enable) new2 |= 0x02u; /* QE bit1 */
    else new2 &= ~(uint8_t)0x02u;
    if (new2 == sr2) return 0; /* no change */
    return qspi_write_sr12(sr1, new2);
}

int qspi_set_address_mode_4byte(bool enable)
{
    /* 大于 16MiB 才需要 4B；此处按调用者需求发命令，并同步控制器地址字节设置 */
    uint32_t cmd = ((enable ? W25Q_CMD_EN4B : W25Q_CMD_EX4B) << CQSPI_CMDCTRL_OPCODE_LSB);
    int rc = qspi_exec_cmd(cmd);
    if (rc) return rc;
    s_addr_bytes = enable ? 4u : 3u;
    uint32_t size = REG32(g_qspi.reg, CQSPI_REG_SIZE);
    size &= ~(CQSPI_SIZE_ADDR_MASK << CQSPI_SIZE_ADDR_LSB);
    size |= (((uint32_t)s_addr_bytes - 1u) & CQSPI_SIZE_ADDR_MASK) << CQSPI_SIZE_ADDR_LSB;
    REG32(g_qspi.reg, CQSPI_REG_SIZE) = size;
    return 0;
}

/* 进入 XIP 1-4-4 模式：使用 0xEB 指令，指令单线、地址四线、数据四线，启用 mode bits */
int qspi_enter_xip_144(unsigned addr_bytes, unsigned dummy_cycles, uint8_t mode_bits)
{
    if (addr_bytes < 3u) addr_bytes = 3u;
    if (addr_bytes > 4u) addr_bytes = 4u;
    if (dummy_cycles == 0u) dummy_cycles = 4u; /* 参考 RBL：0xEB 常用 4 个 dummy */
    if (mode_bits == 0u) mode_bits = 0x20u;      /* 参考 RBL：Mode bits 常用 0x20 */

    /* 1) 退出 DIRECT，保持 ENABLE 置位，确保可安全配置 */
    printf("s300_qspi: enter XIP 1-4-4 (addr_bytes=%u, dummy=%u, mode=0x%02X)\n",
        (unsigned)addr_bytes, (unsigned)dummy_cycles, (unsigned)mode_bits);
    uint32_t cfg = REG32(g_qspi.reg, CQSPI_REG_CONFIG);
    cfg &= ~CQSPI_CFG_DIRECT;
    cfg |= CQSPI_CFG_ENABLE;
    REG32(g_qspi.reg, CQSPI_REG_CONFIG) = cfg;
    if (qspi_wait_idle() != 0) return -1;

    /* 设置地址字节数到 SIZE 寄存器，影响 DIRECT/XIP 地址阶段 */
    uint32_t size = REG32(g_qspi.reg, CQSPI_REG_SIZE);
    size &= ~(CQSPI_SIZE_ADDR_MASK << CQSPI_SIZE_ADDR_LSB);
    size |= (((addr_bytes - 1u) & CQSPI_SIZE_ADDR_MASK) << CQSPI_SIZE_ADDR_LSB);
    REG32(g_qspi.reg, CQSPI_REG_SIZE) = size;

    /* 2) 配置 1-4-4 读取特性，设置 0xEB opcode，启用 mode bits 与 dummy */
    uint32_t rd = ((uint32_t)W25Q_CMD_QUAD_FAST << CQSPI_RD_OPCODE_LSB) |
                  ((uint32_t)CQSPI_INST_TYPE_SINGLE << CQSPI_RD_TYPE_INSTR_LSB) |
                  ((uint32_t)CQSPI_INST_TYPE_QUAD   << CQSPI_RD_TYPE_ADDR_LSB)  |
                  ((uint32_t)CQSPI_INST_TYPE_QUAD   << CQSPI_RD_TYPE_DATA_LSB)  |
                  ((uint32_t)(dummy_cycles & CQSPI_RD_DUMMY_MASK) << CQSPI_RD_DUMMY_LSB) |
                  (1u << CQSPI_RD_MODE_EN_LSB);
    REG32(g_qspi.reg, CQSPI_REG_RD_INSTR) = rd;
    if (qspi_wait_idle() != 0) return -1;

    /* 配置 Mode bits（多数器件可用 0x00） */
    uint32_t mode = REG32(g_qspi.reg, CQSPI_REG_MODE_BIT);
    mode &= ~CQSPI_MODE_BITS_MASK;
    mode |= ((uint32_t)(mode_bits & CQSPI_MODE_BITS_MASK));
    REG32(g_qspi.reg, CQSPI_REG_MODE_BIT) = mode;
    if (qspi_wait_idle() != 0) return -1;

    /* 3) 通过 XIP_NEXT 进入 XIP，再打开 DIRECT 使能 AHB 访问 */
    cfg = REG32(g_qspi.reg, CQSPI_REG_CONFIG);
    cfg &= ~CQSPI_CFG_XIP_IMM;
    cfg |= CQSPI_CFG_XIP_NEXT;
    REG32(g_qspi.reg, CQSPI_REG_CONFIG) = cfg;
    if (qspi_wait_idle() != 0) return -1;

    cfg |= CQSPI_CFG_DIRECT;
    REG32(g_qspi.reg, CQSPI_REG_CONFIG) = cfg;
    if (qspi_wait_idle() != 0) return -1;

    /* 打印进入 XIP 后的关键信息：CFG/REMAP/MODE/RD_INSTR/BAUD/SCLK */
    {
        uint32_t cfg_now = REG32(g_qspi.reg, CQSPI_REG_CONFIG);
        uint32_t remap   = REG32(g_qspi.reg, CQSPI_REG_REMAP);
        uint32_t modev   = REG32(g_qspi.reg, CQSPI_REG_MODE_BIT);
        uint32_t rdins   = REG32(g_qspi.reg, CQSPI_REG_RD_INSTR);
        uint32_t sizev   = REG32(g_qspi.reg, CQSPI_REG_SIZE);
        uint32_t addr_sz = ((sizev >> CQSPI_SIZE_ADDR_LSB) & CQSPI_SIZE_ADDR_MASK) + 1u;
        uint32_t baud    = qspi_get_baud_raw();
        uint32_t sclk    = qspi_get_actual_sclk_hz();
        printf("s300_qspi: XIP entered (CFG=0x%08lX, REMAP=0x%08lX, MODE=0x%08lX, RD_INSTR=0x%08lX)\n",
               (unsigned long)cfg_now, (unsigned long)remap, (unsigned long)modev, (unsigned long)rdins);
        printf("s300_qspi: AHB=0x%08lX, addr_bytes=%lu, BAUD=%lu (SCLK=%lu Hz)\n",
               (unsigned long)(uintptr_t)g_qspi.ahb,
               (unsigned long)addr_sz, (unsigned long)baud, (unsigned long)sclk);
        qspi_dump_modes("after-enter-xip");
    }
    return 0;
}

void qspi_exit_xip_mode(void)
{
    /* 参考 RBL：先关 DIRECT 且清 XIP_NEXT/IMM，再清 MODE bits，必要时再开 DIRECT */
    uint32_t cfg_before = REG32(g_qspi.reg, CQSPI_REG_CONFIG);
    uint32_t mode_before = REG32(g_qspi.reg, CQSPI_REG_MODE_BIT);
    uint32_t rdins_before = REG32(g_qspi.reg, CQSPI_REG_RD_INSTR);
    printf("s300_qspi: exit XIP begin (CFG=0x%08lX, MODE=0x%08lX, RD_INSTR=0x%08lX)\n",
           (unsigned long)cfg_before, (unsigned long)mode_before, (unsigned long)rdins_before);
    uint32_t cfg = REG32(g_qspi.reg, CQSPI_REG_CONFIG);

    cfg &= ~(CQSPI_CFG_DIRECT | CQSPI_CFG_XIP_NEXT | CQSPI_CFG_XIP_IMM);
    REG32(g_qspi.reg, CQSPI_REG_CONFIG) = cfg;
    (void)qspi_wait_idle();

    uint32_t mode = REG32(g_qspi.reg, CQSPI_REG_MODE_BIT);
    mode &= ~CQSPI_MODE_BITS_MASK;
    REG32(g_qspi.reg, CQSPI_REG_MODE_BIT) = mode;
    (void)qspi_wait_idle();

    cfg &= ~(CQSPI_CFG_XIP_NEXT | CQSPI_CFG_XIP_IMM);

    /* 退出连续读取模式：发送 mode bits FFh 以确保 M4=1 */
    uint32_t rd_temp = ((uint32_t)W25Q_CMD_QUAD_FAST << CQSPI_RD_OPCODE_LSB) |
                       ((uint32_t)CQSPI_INST_TYPE_SINGLE << CQSPI_RD_TYPE_INSTR_LSB) |
                       ((uint32_t)CQSPI_INST_TYPE_QUAD << CQSPI_RD_TYPE_ADDR_LSB) |
                       ((uint32_t)CQSPI_INST_TYPE_QUAD << CQSPI_RD_TYPE_DATA_LSB) |
                       (4u << CQSPI_RD_DUMMY_LSB) |
                       (1u << CQSPI_RD_MODE_EN_LSB);
    REG32(g_qspi.reg, CQSPI_REG_RD_INSTR) = rd_temp;
    mode |= 0xFFu;
    REG32(g_qspi.reg, CQSPI_REG_MODE_BIT) = mode;
    (void)qspi_wait_idle();
    /* 发送 dummy Quad Fast Read 命令来应用 mode bits FFh */
    uint32_t cmd = (W25Q_CMD_QUAD_FAST << CQSPI_CMDCTRL_OPCODE_LSB) |
                   (1u << CQSPI_CMDCTRL_ADDR_EN_LSB) |
                   (2u << CQSPI_CMDCTRL_ADD_BYTES_LSB) |
                   (1u << CQSPI_CMDCTRL_RD_EN_LSB) |
                   (0u << CQSPI_CMDCTRL_RD_BYTES_LSB) |
                   (4u << CQSPI_CMDCTRL_DUMMY_LSB);
    REG32(g_qspi.reg, CQSPI_REG_CMDADDRESS) = 0x0;
    (void)qspi_exec_cmd(cmd);
    /* 清除 mode bits */
    mode &= ~CQSPI_MODE_BITS_MASK;
    REG32(g_qspi.reg, CQSPI_REG_MODE_BIT) = mode;
    (void)qspi_wait_idle();

    /* 恢复全单线的 RD_INSTR 且将 opcode 设为标准 0x03 READ，dummy=0，关闭 mode 位。
       这样在需要时可以通过一次 DIRECT AHB 读让器件看到 READ 指令并退出内部连续读状态。 */
    uint32_t rd = ((uint32_t)W25Q_CMD_READ << CQSPI_RD_OPCODE_LSB) |
                  ((uint32_t)CQSPI_INST_TYPE_SINGLE << CQSPI_RD_TYPE_INSTR_LSB) |
                  ((uint32_t)CQSPI_INST_TYPE_SINGLE << CQSPI_RD_TYPE_ADDR_LSB)  |
                  ((uint32_t)CQSPI_INST_TYPE_SINGLE << CQSPI_RD_TYPE_DATA_LSB);
    REG32(g_qspi.reg, CQSPI_REG_RD_INSTR) = rd;

    /* 打印退出后的关键寄存器状态 */
    {
        uint32_t cfg_now = REG32(g_qspi.reg, CQSPI_REG_CONFIG);
        uint32_t modev   = REG32(g_qspi.reg, CQSPI_REG_MODE_BIT);
        uint32_t rdins   = REG32(g_qspi.reg, CQSPI_REG_RD_INSTR);
        printf("s300_qspi: XIP exited  (CFG=0x%08lX, MODE=0x%08lX, RD_INSTR=0x%08lX)\n",
               (unsigned long)cfg_now, (unsigned long)modev, (unsigned long)rdins);
        qspi_dump_modes("after-exit-xip");
    }
}

int qspi_page_program(uint32_t addr, const void *buf, uint32_t len)
{
    if (!buf || len == 0u) return -1;
    if (len > g_qspi.page_size) len = g_qspi.page_size;
    if (s_qspi_verbose) {
        qspi_dump_modes("before-page-program");
        printf("s300_qspi: PAGE PROGRAM addr=0x%08lX len=%lu (page_size=%lu)\n",
               (unsigned long)addr, (unsigned long)len, (unsigned long)g_qspi.page_size);
    }
    int rc;
    
    /* 使用 STIG 模式小块写入 */
    const uint8_t *p8 = (const uint8_t *)buf;
    uint32_t off = 0;
    while (off < len)
    {
        uint32_t chunk = len - off;
        if (chunk > 8u) chunk = 8u;
        rc = qspi_wren();
        if (rc) return rc;
        REG32(g_qspi.reg, CQSPI_REG_CMDADDRESS) = (addr + off);
        uint32_t lower = 0, upper = 0;
        memcpy(&lower, p8 + off, (chunk > 4u) ? 4u : chunk);
        if (chunk > 4u)
        {
            memcpy(&upper, p8 + off + 4u, chunk - 4u);
        }
        REG32(g_qspi.reg, CQSPI_REG_CMDWRITEDATALOWER) = lower;
        REG32(g_qspi.reg, CQSPI_REG_CMDWRITEDATAUPPER) = upper;
    uint32_t cmd = (W25Q_CMD_PP << CQSPI_CMDCTRL_OPCODE_LSB) |
               (1u << CQSPI_CMDCTRL_ADDR_EN_LSB) |
               ((((uint32_t)s_addr_bytes - 1u) & CQSPI_CMDCTRL_ADD_BYTES_MASK) << CQSPI_CMDCTRL_ADD_BYTES_LSB) |
                       (1u << CQSPI_CMDCTRL_WR_EN_LSB) |
                       (((chunk - 1u) & CQSPI_CMDCTRL_WR_BYTES_MASK) << CQSPI_CMDCTRL_WR_BYTES_LSB);
        rc = qspi_exec_cmd(cmd);
        if (rc) return rc;
        rc = qspi_wait_ready(20u);
        if (rc) return rc;
        off += chunk;
    }
    return 0;
}

int qspi_read(uint32_t addr, void *buf, uint32_t len)
{
    if (!buf || len == 0u) return -1;
    if (s_qspi_verbose) {
        qspi_dump_modes("before-read");
        printf("s300_qspi: READ addr=0x%08lX len=%lu\n", (unsigned long)addr, (unsigned long)len);
    }
    
    /* 使用 STIG FAST READ 0x0B 分块读取 */
    uint8_t *pp = (uint8_t *)buf;
    uint32_t a = addr;
    uint32_t remain = len;
    while (remain)
    {
        uint32_t chunk = (remain > 8u) ? 8u : remain;  /* 保持8字节以确保稳定性 */
        REG32(g_qspi.reg, CQSPI_REG_CMDADDRESS) = a;
    uint32_t cmd = (W25Q_CMD_FAST << CQSPI_CMDCTRL_OPCODE_LSB) |
               (1u << CQSPI_CMDCTRL_ADDR_EN_LSB) |
               ((((uint32_t)s_addr_bytes - 1u) & CQSPI_CMDCTRL_ADD_BYTES_MASK) << CQSPI_CMDCTRL_ADD_BYTES_LSB) |
                       (1u << CQSPI_CMDCTRL_RD_EN_LSB) |
                       (((chunk - 1u) & CQSPI_CMDCTRL_RD_BYTES_MASK) << CQSPI_CMDCTRL_RD_BYTES_LSB) |
                       (8u << CQSPI_CMDCTRL_DUMMY_LSB);  /* 8 dummy cycles for FAST READ */
        int r = qspi_exec_cmd(cmd);
        if (r) return r;
        uint32_t low = REG32(g_qspi.reg, CQSPI_REG_CMDREADDATALOWER);
        uint32_t take = (chunk > 4u) ? 4u : chunk;
        memcpy(pp, &low, take);
        if (chunk > 4u)
        {
            uint32_t up = REG32(g_qspi.reg, CQSPI_REG_CMDREADDATAUPPER);
            memcpy(pp + 4u, &up, chunk - 4u);
        }
        pp += chunk;
        a += chunk;
        remain -= chunk;
    }
    if (s_qspi_verbose) {
        qspi_dump_modes("after-read");
    }
    return 0;
}

/* --- Debug helpers --- */

static int qspi_read_one(uint8_t opcode, uint8_t *val)
{
    if (!val) return -1;
    uint32_t cmd = ((uint32_t)opcode << CQSPI_CMDCTRL_OPCODE_LSB) |
                   (1u << CQSPI_CMDCTRL_RD_EN_LSB) |
                   (((1u - 1u) & CQSPI_CMDCTRL_RD_BYTES_MASK) << CQSPI_CMDCTRL_RD_BYTES_LSB);
    int rc = qspi_exec_cmd(cmd);
    if (rc) return rc;
    *val = (uint8_t)(REG32(g_qspi.reg, CQSPI_REG_CMDREADDATALOWER) & 0xFFu);
    return 0;
}

void qspi_dump_regs(const char *tag)
{
    if (!tag) tag = "";
    uint32_t cfg = REG32(g_qspi.reg, CQSPI_REG_CONFIG);
    uint32_t rdinstr = REG32(g_qspi.reg, CQSPI_REG_RD_INSTR);
    uint32_t wrinstr = REG32(g_qspi.reg, CQSPI_REG_WR_INSTR);
    uint32_t size = REG32(g_qspi.reg, CQSPI_REG_SIZE);
    uint32_t delay = REG32(g_qspi.reg, CQSPI_REG_DELAY);
    uint32_t rdcap = REG32(g_qspi.reg, CQSPI_REG_RD_DATA_CAPTURE);
    uint32_t part = REG32(g_qspi.reg, CQSPI_REG_SRAMPARTITION);
    uint32_t remap = REG32(g_qspi.reg, CQSPI_REG_REMAP);
    uint32_t modeb = REG32(g_qspi.reg, CQSPI_REG_MODE_BIT);
    uint32_t sdram = REG32(g_qspi.reg, CQSPI_REG_SDRAMLEVEL);
    uint32_t irqst = REG32(g_qspi.reg, CQSPI_REG_IRQSTATUS);
    printf("[QSPI] dump %s\n", tag);
    printf("  CFG=%08lX SIZE=%08lX DELAY=%08lX RD_CAP=%08lX PART=%08lX\n",
           (unsigned long)cfg, (unsigned long)size, (unsigned long)delay,
           (unsigned long)rdcap, (unsigned long)part);
    printf("  RD_INSTR=%08lX WR_INSTR=%08lX REMAP=%08lX MODE=%08lX\n",
           (unsigned long)rdinstr, (unsigned long)wrinstr,
           (unsigned long)remap, (unsigned long)modeb);
    printf("  SDRAM=%08lX IRQSTS=%08lX\n",
           (unsigned long)sdram, (unsigned long)irqst);
}

/* 详细解码当前控制器模式配置（RD_INSTR/WR_INSTR/SIZE/DELAY/RD_CAPTURE/BAUD等） */
static void qspi_dump_modes(const char *tag)
{
    if (!tag) tag = "";
    uint32_t cfg    = REG32(g_qspi.reg, CQSPI_REG_CONFIG);
    uint32_t size   = REG32(g_qspi.reg, CQSPI_REG_SIZE);
    uint32_t rdins  = REG32(g_qspi.reg, CQSPI_REG_RD_INSTR);
    uint32_t wrins  = REG32(g_qspi.reg, CQSPI_REG_WR_INSTR);
    uint32_t delay  = REG32(g_qspi.reg, CQSPI_REG_DELAY);
    uint32_t cap    = REG32(g_qspi.reg, CQSPI_REG_RD_DATA_CAPTURE);
    uint32_t remap  = REG32(g_qspi.reg, CQSPI_REG_REMAP);
    uint32_t modeb  = REG32(g_qspi.reg, CQSPI_REG_MODE_BIT);
    uint32_t baud   = qspi_get_baud_raw();
    uint32_t sclk   = qspi_get_actual_sclk_hz();

    uint32_t addr_bytes = ((size >> CQSPI_SIZE_ADDR_LSB) & CQSPI_SIZE_ADDR_MASK) + 1u;
    uint32_t rd_op   = (rdins >> CQSPI_RD_OPCODE_LSB) & CQSPI_RD_OPCODE_MASK;
    uint32_t rd_ti   = (rdins >> CQSPI_RD_TYPE_INSTR_LSB) & CQSPI_RD_TYPE_INSTR_MASK;
    uint32_t rd_ta   = (rdins >> CQSPI_RD_TYPE_ADDR_LSB)  & CQSPI_RD_TYPE_ADDR_MASK;
    uint32_t rd_td   = (rdins >> CQSPI_RD_TYPE_DATA_LSB)  & CQSPI_RD_TYPE_DATA_MASK;
    uint32_t rd_md   = (rdins >> CQSPI_RD_MODE_EN_LSB)    & 0x1u;
    uint32_t rd_dm   = (rdins >> CQSPI_RD_DUMMY_LSB)      & CQSPI_RD_DUMMY_MASK;

    uint32_t wr_op   = (wrins >> CQSPI_WR_OPCODE_LSB) & CQSPI_WR_OPCODE_MASK;
    uint32_t wr_ta   = (wrins >> CQSPI_WR_TYPE_ADDR_LSB) & CQSPI_WR_TYPE_ADDR_MASK;
    uint32_t wr_td   = (wrins >> CQSPI_WR_TYPE_DATA_LSB) & CQSPI_WR_TYPE_DATA_MASK;
    uint32_t wr_dm   = (wrins >> CQSPI_WR_DUMMY_LSB)     & CQSPI_WR_DUMMY_MASK;

    uint32_t tshsl = (delay >> CQSPI_DELAY_TSHSL_LSB) & CQSPI_DELAY_TSHSL_MASK;
    uint32_t tchsh = (delay >> CQSPI_DELAY_TCHSH_LSB) & CQSPI_DELAY_TCHSH_MASK;
    uint32_t tslch = (delay >> CQSPI_DELAY_TSLCH_LSB) & CQSPI_DELAY_TSLCH_MASK;
    uint32_t tsd2d = (delay >> CQSPI_DELAY_TSD2D_LSB) & CQSPI_DELAY_TSD2D_MASK;

    uint32_t cap_bypass = (cap & CQSPI_RD_CAPTURE_BYPASS) ? 1u : 0u;
    uint32_t cap_delay  = (cap >> CQSPI_RD_CAPTURE_DELAY_LSB) & CQSPI_RD_CAPTURE_DELAY_MASK;
    uint32_t cap_edge   = (cap & CQSPI_RD_CAPTURE_SAMPLE_EDGE) ? 1u : 0u;

    printf("s300_qspi: modes %s\n", tag);
    printf("  CFG=0x%08lX (DIRECT=%u XIP_NEXT=%u XIP_IMM=%u) REMAP=0x%08lX SIZE.addr_bytes=%lu\n",
        (unsigned long)cfg,
        (unsigned)(((cfg & CQSPI_CFG_DIRECT) != 0)),
        (unsigned)(((cfg & CQSPI_CFG_XIP_NEXT) != 0)),
        (unsigned)(((cfg & CQSPI_CFG_XIP_IMM) != 0)),
        (unsigned long)remap,
        (unsigned long)addr_bytes);
    printf("  RD_INSTR: OPCODE=0x%02lX ITYPE=%lu ATYPE=%lu DTYPE=%lu MODE_EN=%lu DUMMY=%lu\n",
        (unsigned long)rd_op, (unsigned long)rd_ti, (unsigned long)rd_ta,
        (unsigned long)rd_td, (unsigned long)rd_md, (unsigned long)rd_dm);
    printf("  WR_INSTR: OPCODE=0x%02lX ATYPE=%lu DTYPE=%lu DUMMY=%lu\n",
        (unsigned long)wr_op, (unsigned long)wr_ta, (unsigned long)wr_td, (unsigned long)wr_dm);
    printf("  DELAY: TSHSL=%lu TCHSH=%lu TSLCH=%lu TSD2D=%lu\n",
        (unsigned long)tshsl, (unsigned long)tchsh, (unsigned long)tslch, (unsigned long)tsd2d);
    printf("  RD_CAPTURE: BYPASS=%lu DELAY=%lu SAMPLE_EDGE=%lu\n",
        (unsigned long)cap_bypass, (unsigned long)cap_delay, (unsigned long)cap_edge);
    printf("  MODE=0x%08lX BAUD=%lu SCLK=%lu Hz\n",
        (unsigned long)modeb, (unsigned long)baud, (unsigned long)sclk);
}

int qspi_read_status(uint8_t *sr1, uint8_t *sr2, uint8_t *sr3)
{
    int rc = 0;
    if (sr1)
    {
        rc = qspi_read_one(W25Q_CMD_RDSR1, sr1);
        if (rc) return rc;
    }
    if (sr2)
    {
        rc = qspi_read_one(W25Q_CMD_RDSR2, sr2);
        if (rc) return rc;
    }
    if (sr3)
    {
        rc = qspi_read_one(W25Q_CMD_RDSR3, sr3);
        if (rc) return rc;
    }
    return 0;
}

int qspi_set_sram_partition(uint32_t read_locations)
{
    /* Per spec: program while controller is idle */
    if (qspi_wait_idle() != 0) return -1;

    uint32_t min_loc = 1u + 1u; /* avoid 0 -> means 1 location; we want at least 2 locations */
    uint32_t max_loc = CQSPI_SRAM_TOTAL_LOCATIONS - 1u; /* avoid max (all read, 0 write) */
    if (read_locations < min_loc) read_locations = min_loc;
    if (read_locations > max_loc) read_locations = max_loc;

    /* Program register: value is (read_locations - 1) according to spec */
    uint32_t regv = (read_locations - 1u) & CQSPI_SRAM_PARTITION_MASK;
    REG32(g_qspi.reg, CQSPI_REG_SRAMPARTITION) = regv;
    return 0;
}

void qspi_get_sram_partition(uint32_t *read_locations, uint32_t *write_locations)
{
    uint32_t regv = REG32(g_qspi.reg, CQSPI_REG_SRAMPARTITION) & CQSPI_SRAM_PARTITION_MASK;
    uint32_t read_loc = regv + 1u;
    uint32_t write_loc = CQSPI_SRAM_TOTAL_LOCATIONS - regv;
    if (read_locations) *read_locations = read_loc;
    if (write_locations) *write_locations = write_loc;
}