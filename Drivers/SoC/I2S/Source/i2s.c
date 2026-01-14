#include "i2s.h"
#include "dma.h"
#include "dma_s300.h"
#include "rcc.h"

static inline S300_I2S_TypeDef *i2s_dev(i2s_idx_t i)
{
    return (i == I2S_IDX0) ? I2S0 : I2S1;
}

void i2s_clock_enable(i2s_idx_t i, bool en)
{
    S300_I2S_TypeDef *I = i2s_dev(i);
    if (en) I->CER |= 0x1u;
    else I->CER &= ~0x1u;
}

/* 新增：按位选择修改 cycles/gating，兼容原驱动的 pro 语义 */
void i2s_set_sclk_pro(i2s_idx_t i, uint32_t pro, uint8_t sclk_cycles, uint8_t sclk_gate)
{
    S300_I2S_TypeDef *I = i2s_dev(i);
    I->CER = 0x0u; /* 修改前关时钟 */
    uint32_t v = I->CCR;
    if (pro & 0x1u) /* EM_I2S_SCLK_CYCLYS */
    {
        v &= ~0x18u;
        v |= (((uint32_t)sclk_cycles) & 0x18u);
    }
    if (pro & 0x2u) /* EM_I2S_SCLK_GATING_CYCLYS */
    {
        v &= ~0x7u;
        v |= (((uint32_t)sclk_gate) & 0x7u);
    }
    I->CCR = v;
    I->CER = 0x1u; /* 修改后开时钟 */
}

void i2s_set_sclk(i2s_idx_t i, uint8_t sclk_cycles, uint8_t sclk_gate)
{
    /* 默认同时修改 cycles 与 gating，与旧包装层 set_i2s_sclk_cycles(… , CYC|GATE, …) 对齐 */
    i2s_set_sclk_pro(i, 0x1u | 0x2u, sclk_cycles, sclk_gate);
}

void i2s_set_wordlen(i2s_idx_t i, i2s_word_t word)
{
    S300_I2S_TypeDef *I = i2s_dev(i);
    /* Disable enables for reconfig */
    I->RER0 &= ~1u;
    I->TER0 &= ~1u;
    I->RCR0 = (uint32_t)word;
    I->TCR0 = (uint32_t)word;
    I->RER0 |= 1u;
    I->TER0 |= 1u;
}

/* 与原驱动 set_i2s_resolution_bit 行为一致：按方向(pro)设置并可选同步 IRER/ITER（TDM） */
void i2s_set_wordlen_dir(i2s_idx_t i, uint32_t pro, i2s_word_t word, bool isTDM)
{
    S300_I2S_TypeDef *I = i2s_dev(i);
    bool is_tx = ((pro & 0x04u) != 0u);
    if (is_tx)
    {
        /* 先关闭对应 TX 使能（如需 TDM，同步 ITER） */
        I->TER0 &= ~1u;
        if (isTDM) I->ITER &= ~1u;
        /* 写 TX 位宽配置 */
        I->TCR0 = (uint32_t)word;
        /* 重新开启 */
        I->TER0 |= 1u;
        if (isTDM) I->ITER |= 1u;
    }
    else
    {
        /* 先关闭 RX（如需 TDM，同步 IRER） */
        I->RER0 &= ~1u;
        if (isTDM) I->IRER &= ~1u;
        /* 写 RX 位宽配置 */
        I->RCR0 = (uint32_t)word;
        /* 重新开启 */
        I->RER0 |= 1u;
        if (isTDM) I->IRER |= 1u;
    }
}

void i2s_basic_init(i2s_idx_t i, uint32_t mclk_hz, i2s_word_t word)
{
    /* Legacy: only enable I2S CER here, no extra RCC ops */
    i2s_clock_enable(i, true);
    /* Select SCLK cycles per word length, compute fsck = 15k * 2 * wordlen */
    uint32_t fsck;
    if (word == I2S_WORD_16 || word == I2S_WORD_12)
    {
        i2s_set_sclk(i, 0, 0);
        fsck = 15000u * 2u * 16u;
    }
    else if (word == I2S_WORD_20 || word == I2S_WORD_24)
    {
        i2s_set_sclk(i, 8, 0);
        fsck = 15000u * 2u * 24u;
    }
    else
    {
        i2s_set_sclk(i, 16, 0);
        fsck = 15000u * 2u * 32u;
    }
    /* div reg: CM4_I2S_CLK_DIV = div, actual division is 2*(div+1). Legacy: clk_div = mclk/fsck; set (clk_div/2 -1) */
    uint32_t clk_div = (mclk_hz / fsck);
    rcc_set_i2s_clock((uint8_t)((clk_div / 2u) - 1u));
    /* Set word length then enable Rx/Tx blocks (legacy sequence) */
    i2s_set_wordlen(i, word);
    i2s_tx_enable(i, true);
    i2s_rx_enable(i, true);
}

void i2s_enable(i2s_idx_t i, bool en)
{
    S300_I2S_TypeDef *I = i2s_dev(i);
    if (en)
    {
        i2s_fifo_flush(i, true, true);
        I->IER |= 0x1u; /* Legacy: only IER here */
    }
    else I->IER &= ~0x1u;
}

void i2s_tx_enable(i2s_idx_t i, bool en)
{
    S300_I2S_TypeDef *I = i2s_dev(i);
    if (en) I->ITER |= 0x1u;
    else I->ITER &= ~0x1u;
}

void i2s_rx_enable(i2s_idx_t i, bool en)
{
    S300_I2S_TypeDef *I = i2s_dev(i);
    if (en) I->IRER |= 0x1u;
    else I->IRER &= ~0x1u;
}

void i2s_fifo_flush(i2s_idx_t i, bool rx, bool tx)
{
    S300_I2S_TypeDef *I = i2s_dev(i);
    if (rx) I->RXFFR = 1u;
    else I->RXFFR = 0u;
    if (tx) I->TXFFR = 1u;
    else I->TXFFR = 0u;
}

void i2s_set_dma(i2s_idx_t i, bool tx_en, bool rx_en)
{
    S300_I2S_TypeDef *I = i2s_dev(i);
    /* 同步手册：不仅要置块级位 [17:16]，也要置对应通道位：TX ch0->[8]，RX ch0->[0] */
    uint32_t v = I->DMACR;
    /* 清理块级与 ch0 的通道位，保留其它位 */
    v &= ~((0x3u << 16) | (0xFu << 8) | 0xFu);
    if (tx_en)
    {
        v |= (1u << 17); /* TX block DMA enable */
        v |= (1u << 8);  /* TX channel0 DMA enable */
    }
    if (rx_en)
    {
        v |= (1u << 16); /* RX block DMA enable */
        v |= (1u << 0);  /* RX channel0 DMA enable */
    }
    I->DMACR = v;
}

uint32_t i2s_sr(i2s_idx_t i)
{
    return i2s_dev(i)->SR;
}

void i2s_write_stereo(i2s_idx_t i, uint16_t l, uint16_t r)
{
    S300_I2S_TypeDef *I = i2s_dev(i);
    /* 结构体将 LRBR0/RRBR0 复用为 TX holding 寄存器别名，直接写入即可 */
    I->LRBR0 = l;
    I->RRBR0 = r;
}

uint32_t i2s_read_stereo(i2s_idx_t i)
{
    S300_I2S_TypeDef *I = i2s_dev(i);
    /* Read from RX buffer registers */
    uint32_t l = I->LRBR0 & 0xFFFFu; /* LRBR0 */
    uint32_t r = I->RRBR0 & 0xFFFFu; /* RRBR0 */
    return (l << 16) | r;
}

void i2s_set_interrupt_mask(i2s_idx_t i, uint32_t mask, bool en)
{
    S300_I2S_TypeDef *I = i2s_dev(i);
    if (en) I->IMR0 |= mask;
    else I->IMR0 &= ~mask;
}

/* DMA helper，顺序与旧源码完全一致 */
void i2s_dma_mode(i2s_idx_t i, uint8_t rec_chn, uint8_t play_chn, uint8_t *rec_buffer, uint8_t *play_buffer, uint32_t length)
{
    S300_I2S_TypeDef *I = i2s_dev(i);
    /* 1) 先配置录音通道（源：I2S_RXDMA → 目的：内存） */
    set_dma_std(EM_DMA0, rec_chn, (uint32_t)&I->RXDMA, (uint32_t)rec_buffer, length, EM_TR_WIDTH_16_BIT);
    set_dma_std_increment(EM_DMA0, rec_chn, EM_ADDRESS_UNCHANGE, EM_ADDRESS_INC);
    set_dma_std_transfer_type(EM_DMA0, rec_chn, EM_TR_TYPE_P2M_FD);
    /* 录音通道突发：显式 1B/1B，确保 CTL_L=0x00200413 */
    set_dma_burst_size(EM_DMA0, rec_chn, EM_MSIZE_1B, EM_MSIZE_1B);
    /* 旧源码固定 I2S1 握手，这里按相同操作使用 I2S1_RX */
    set_dma_handshaking(EM_DMA0, rec_chn, EM_HAND_I2S1_RX, EM_HAND_NULL);
    set_dma_interrupt(EM_DMA0, rec_chn, EM_DMA_INT_TFR, 1);
    /* 2) 再配置播放通道（源：内存 → 目的：I2S_TXDMA） */
    set_dma_std(EM_DMA0, play_chn, (uint32_t)play_buffer, (uint32_t)&I->TXDMA, length, EM_TR_WIDTH_16_BIT);
    set_dma_std_increment(EM_DMA0, play_chn, EM_ADDRESS_INC, EM_ADDRESS_UNCHANGE);
    set_dma_std_transfer_type(EM_DMA0, play_chn, EM_TR_TYPE_M2P_FD);
    /* 播放通道突发：SRC=1B, DST=1B（对齐参考 demo） */
    set_dma_burst_size(EM_DMA0, play_chn, EM_MSIZE_1B, EM_MSIZE_1B);
    set_dma_handshaking(EM_DMA0, play_chn, EM_HAND_NULL, EM_HAND_I2S1_TX);
    set_dma_interrupt(EM_DMA0, play_chn, EM_DMA_INT_TFR, 1);
    /* 3) HS_SEL 位对齐：rec 通道置 DST_HS_SEL(0x0400)，play 通道置 SRC_HS_SEL(0x0800) */
    {
        S300_DMA_TypeDef *D = DMAC0;
        D->CH[rec_chn].CFG_L |= (1u << DMA_CFGL_HS_SEL_DST_Pos);
        D->CH[play_chn].CFG_L |= (1u << DMA_CFGL_HS_SEL_SRC_Pos);
    }
    /* 4) I2S DMACR 仅开 TX/RX DMA 使能位 */
    I->DMACR = (1u << 17) | (1u << 16);
    /* 5) 依次启动：rec → play（顺序与旧源码一致） */
    set_dma_start(EM_DMA0, rec_chn);
    set_dma_start(EM_DMA0, play_chn);
}
