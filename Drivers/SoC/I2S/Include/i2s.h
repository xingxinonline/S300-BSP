#ifndef S300_BSP_I2S_H
#define S300_BSP_I2S_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "s300.h"
#include "i2s_s300.h"

typedef enum { I2S_IDX0 = 0, I2S_IDX1 = 1 } i2s_idx_t;

typedef enum { I2S_WORD_12 = 1, I2S_WORD_16 = 2, I2S_WORD_20 = 3, I2S_WORD_24 = 4, I2S_WORD_32 = 5 } i2s_word_t;

/* --- Legacy compatibility defines (from cortex-m4-i2s/driver/i2s.h) --- */
#ifndef EM_I2S_COMPAT
#define EM_I2S_COMPAT 1
enum { EM_I2S_RX = 0x00, EM_I2S_TX = 0x04 };
enum { EM_I2S_MASK_TXFOM = 0x20, EM_I2S_MASK_TXFEM = 0x10, EM_I2S_MASK_RXFOM = 0x02, EM_I2S_MASK_RXDAM = 0x01 };
enum { EM_I2S_WORD_IGNORE = 0, EM_I2S_WORD_12BIT = 1, EM_I2S_WORD_16BIT = 2, EM_I2S_WORD_20BIT = 3, EM_I2S_WORD_24BIT = 4, EM_I2S_WORD_32BIT = 5 };
enum { EM_I2S_SCLK_CYCLYS = 0x1, EM_I2S_SCLK_GATING_CYCLYS = 0x2 };
#endif

/* Basic controls */
void i2s_clock_enable(i2s_idx_t i, bool en);
void i2s_set_sclk(i2s_idx_t i, uint8_t sclk_cycles, uint8_t sclk_gate);
void i2s_set_sclk_pro(i2s_idx_t i, uint32_t pro, uint8_t sclk_cycles, uint8_t sclk_gate);
void i2s_basic_init(i2s_idx_t i, uint32_t mclk_hz, i2s_word_t word);
void i2s_enable(i2s_idx_t i, bool en);
void i2s_tx_enable(i2s_idx_t i, bool en);
void i2s_rx_enable(i2s_idx_t i, bool en);
void i2s_fifo_flush(i2s_idx_t i, bool rx, bool tx);
void i2s_set_wordlen(i2s_idx_t i, i2s_word_t word);
void i2s_set_wordlen_dir(i2s_idx_t i, uint32_t pro, i2s_word_t word, bool isTDM);
void i2s_set_dma(i2s_idx_t i, bool tx_en, bool rx_en);
void i2s_set_interrupt_mask(i2s_idx_t i, uint32_t mask, bool en);
void i2s_dma_mode(i2s_idx_t i, uint8_t rec_chn, uint8_t play_chn, uint8_t *rec_buffer, uint8_t *play_buffer, uint32_t length);
uint32_t i2s_sr(i2s_idx_t i);
void i2s_write_stereo(i2s_idx_t i, uint16_t l, uint16_t r);
uint32_t i2s_read_stereo(i2s_idx_t i);

/* RCC helpers (from BSP RCC) */
void rcc_set_i2s_clock(uint8_t div);

/* Legacy inline aliases for smooth porting (optional) */
static inline void set_i2s_clock_enable(int i, int en)
{
    i2s_clock_enable((i2s_idx_t)i, en != 0);
}
static inline void set_i2s_sclk_cycles(int i, uint32_t pro, uint32_t cyc, uint32_t gate)
{
    i2s_set_sclk_pro((i2s_idx_t)i, pro, (uint8_t)cyc, (uint8_t)gate);
}
static inline void set_i2s_Fifo_flushes(int i, uint32_t pro, int en)
{
    if ((pro & 0x04u) == 0) i2s_fifo_flush((i2s_idx_t)i, en != 0, false);
    else i2s_fifo_flush((i2s_idx_t)i, false, en != 0);
}
static inline void set_i2s_resolution_bit(int i, uint32_t pro, uint32_t word, int isTDM)
{
    i2s_set_wordlen_dir((i2s_idx_t)i, pro, (i2s_word_t)word, isTDM != 0);
}
static inline void set_i2s_interrupt_mask(int i, uint32_t m, int en)
{
    i2s_set_interrupt_mask((i2s_idx_t)i, m, en != 0);
}
static inline void set_i2s_en(int i, int en)
{
    i2s_enable((i2s_idx_t)i, en != 0);
}
static inline void set_i2s_stereo_data(int i, uint16_t l, uint16_t r)
{
    i2s_write_stereo((i2s_idx_t)i, l, r);
}
static inline uint32_t get_i2s_stereo_data(int i)
{
    return i2s_read_stereo((i2s_idx_t)i);
}
static inline void set_i2s_transmitter_block_en(int i, int en)
{
    i2s_tx_enable((i2s_idx_t)i, en != 0);
}
static inline void set_i2s_recvier_block_en(int i, int en)
{
    i2s_rx_enable((i2s_idx_t)i, en != 0);
}
static inline int get_i2s_transmitter_fifo_empty_status(int i)
{
    return (int)i2s_sr((i2s_idx_t)i);
}
static inline void set_i2s_basic_config(int i, uint32_t mclk, uint32_t word)
{
    i2s_basic_init((i2s_idx_t)i, mclk, (i2s_word_t)word);
}
static inline void set_i2s_dma_mask(int i, int tx_en, int rx_en)
{
    i2s_set_dma((i2s_idx_t)i, tx_en != 0, rx_en != 0);
}
static inline void set_i2s_dma_mode(int i, uint8_t rec_chn, uint8_t play_chn, uint8_t *rec_buffer, uint8_t *play_buffer, uint32_t length)
{
    i2s_dma_mode((i2s_idx_t)i, rec_chn, play_chn, rec_buffer, play_buffer, length);
}

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_I2S_H */
