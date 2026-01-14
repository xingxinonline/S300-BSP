#ifndef S300_BSP_DMA_H
#define S300_BSP_DMA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "s300.h"
#include "dma_s300.h"

typedef enum { DMA_IDX0 = 0, DMA_IDX1 = 1 } dma_idx_t;

typedef enum { DMA_WIDTH_8 = 0, DMA_WIDTH_16 = 1, DMA_WIDTH_32 = 2 } dma_width_t;
typedef enum { DMA_ADDR_INC = 0, DMA_ADDR_DEC = 1, DMA_ADDR_KEEP = 2 } dma_inc_t;
typedef enum { DMA_MSIZE_1 = 0, DMA_MSIZE_4 = 1, DMA_MSIZE_8 = 2, DMA_MSIZE_16 = 3, DMA_MSIZE_32 = 4, DMA_MSIZE_64 = 5, DMA_MSIZE_128 = 6, DMA_MSIZE_256 = 7 } dma_msize_t;

typedef enum
{
    DMA_TR_TYPE_M2M_FD = 0,
    DMA_TR_TYPE_M2P_FD = 1,
    DMA_TR_TYPE_P2M_FD = 2,
    DMA_TR_TYPE_P2P_FD = 3,
    DMA_TR_TYPE_P2M_FP = 4,
    DMA_TR_TYPE_P2P_FSP = 5,
    DMA_TR_TYPE_M2P_FP = 6,
    DMA_TR_TYPE_P2P_FDP = 7,
} dma_tr_type_t;

typedef enum { DMA_INT_BLOCK = 0x01, DMA_INT_SRCTRAN = 0x02, DMA_INT_DSTTRAN = 0x04, DMA_INT_TFR = 0x08 } dma_int_t;

typedef struct
{
    uint32_t SAR;
    uint32_t DAR;
    uint32_t LLP;
    uint32_t CTL_L;
    uint32_t CTL_H;
    uint32_t DSTAT; /* minimize to match variant without SSTAT field */
} dma_lli_t;

/*
 * LLI usage notes:
 * - BLOCK_TS is limited by hardware field width (driver programs 12 bits -> 0xFFF transfers per block).
 *   Max bytes per block = 0xFFF * bytes_per_transfer, where bytes_per_transfer is 1/2/4 for 8/16/32-bit width.
 * - In LLI mode, after each block completes, CTL is reloaded from the LLI entry. Therefore, if you want
 *   to continue following LLP, you MUST set CTL_L.LLP_SRC_EN and CTL_L.LLP_DST_EN inside EACH LLI entry.
 * - Align addresses and length to the transfer width (1/2/4). Many controllers also require LLP address
 *   alignment (e.g., 8-byte). Provide properly aligned dma_lli_t memory.
 */

int dma_init(dma_idx_t d);
int dma_set_std(dma_idx_t d, uint8_t ch, uint32_t src, uint32_t dst, uint32_t len, dma_width_t width);
void dma_set_link_unit(dma_lli_t *lli, uint32_t src, uint32_t dst, uint32_t len, dma_width_t width);
int dma_set_link(dma_idx_t d, uint8_t ch, dma_lli_t *lli);
void dma_start(dma_idx_t d, uint8_t ch);
void dma_set_transfer_type(dma_idx_t d, uint8_t ch, dma_tr_type_t type);
int dma_set_handshaking(dma_idx_t d, uint8_t ch, uint16_t src_hs, uint16_t dst_hs);
void dma_set_increment(dma_idx_t d, uint8_t ch, dma_inc_t s_inc, dma_inc_t d_inc);
void dma_set_burst(dma_idx_t d, uint8_t ch, dma_msize_t s_msize, dma_msize_t d_msize);
void dma_set_width(dma_idx_t d, uint8_t ch, dma_width_t s_w, dma_width_t d_w);
void dma_set_interrupt(dma_idx_t d, uint8_t ch, dma_int_t type, bool en);
bool dma_is_busy(dma_idx_t d, uint8_t ch);
void dma_set_reload(dma_idx_t d, uint8_t ch, bool s_reload, bool d_reload);
void dma_stop(dma_idx_t d, uint8_t ch);
void dma_set_address(dma_idx_t d, uint8_t ch, uint32_t src, uint32_t dst);

/* Scatter/Gather configuration
 * SGRx: [31:20] SGC (source gather count), [19:0] SGI (source gather interval)
 * DSRx: [31:20] DSC (destination scatter count), [19:0] DSI (destination scatter interval)
 * Units are in transfers of CTLx.SRC_TR_WIDTH or CTLx.DST_TR_WIDTH respectively.
 * Passing count=0 or interval=0 disables the feature on the respective side.
 */
void dma_set_source_gather(dma_idx_t d, uint8_t ch, uint32_t sgc, uint32_t sgi);
void dma_set_dest_scatter(dma_idx_t d, uint8_t ch, uint32_t dsc, uint32_t dsi);

/* -------- Convenience helpers for large memcpy using LLI chaining --------
 * API contract:
 * - Caller provides an array of LLI entries and its capacity.
 * - Function will split the [src,dst,len] into multiple blocks (<= BLOCK_TS limit) and build a chain.
 * - It sets up the channel (M2M, INC/INC, widths), programs LLP, starts the transfer.
 * - Non-blocking variant returns immediately; use dma_is_busy() to poll completion.
 * - Returns 0 on success; negative on errors:
 *     -1: invalid channel
 *     -2: alignment error (src/dst/len not aligned to width)
 *     -3: insufficient LLI capacity
 *     -4: LLI base alignment requirement not met (e.g., not 8-byte aligned)
 */
uint32_t dma_calc_lli_count(uint32_t len, dma_width_t width);
int dma_memcpy_lli(dma_idx_t d, uint8_t ch, uint32_t src, uint32_t dst, uint32_t len,
                   dma_width_t width, dma_lli_t *llis, uint32_t lli_capacity);
int dma_memcpy_lli_blocking(dma_idx_t d, uint8_t ch, uint32_t src, uint32_t dst, uint32_t len,
                            dma_width_t width, dma_lli_t *llis, uint32_t lli_capacity);

/* Legacy alias wrappers to ease porting */
typedef enum { EM_DMA0 = 0, EM_DMA1 = 1 } emDMA;
typedef enum { EM_TR_WIDTH_8_BIT = 0, EM_TR_WIDTH_16_BIT = 1, EM_TR_WIDTH_32_BIT = 2 } emDMATRWIDTH;
typedef enum { EM_ADDRESS_INC = 0, EM_ADDRESS_DEC = 1, EM_ADDRESS_UNCHANGE = 2 } emDMAINCDIR;
typedef enum { EM_MSIZE_1B = 0, EM_MSIZE_4B = 1, EM_MSIZE_8B = 2, EM_MSIZE_16B = 3, EM_MSIZE_32B = 4, EM_MSIZE_64B = 5, EM_MSIZE_128B = 6, EM_MSIZE_256B = 7 } emDMAMSIZE;
typedef enum { EM_TR_TYPE_M2M_FD = 0, EM_TR_TYPE_M2P_FD = 1, EM_TR_TYPE_P2M_FD = 2, EM_TR_TYPE_P2P_FD = 3, EM_TR_TYPE_P2M_FP = 4, EM_TR_TYPE_P2P_FSP = 5, EM_TR_TYPE_M2P_FP = 6, EM_TR_TYPE_P2P_FDP = 7 } emDMATRTYPE;
typedef enum { EM_DMA_INT_BLOCK = 0x01, EM_DMA_INT_SRCTRAN = 0x02, EM_DMA_INT_DSTTRAN = 0x04, EM_DMA_INT_TFR = 0x08 } emDMAIntType;

static inline int init_dma(emDMA dma)
{
    return dma_init((dma_idx_t)dma);
}
static inline int set_dma_std(emDMA dma, uint8_t ch, uint32_t src, uint32_t dst, uint32_t len, emDMATRWIDTH w)
{
    return dma_set_std((dma_idx_t)dma, ch, src, dst, len, (dma_width_t)w);
}
static inline void set_dma_link_unit(dma_lli_t *lli, uint32_t s, uint32_t d, uint32_t l, emDMATRWIDTH w)
{
    dma_set_link_unit(lli, s, d, l, (dma_width_t)w);
}
static inline int set_dma_link(emDMA dma, uint8_t ch, dma_lli_t *lli)
{
    return dma_set_link((dma_idx_t)dma, ch, lli);
}
static inline void set_dma_start(emDMA dma, uint8_t ch)
{
    dma_start((dma_idx_t)dma, ch);
}
static inline void set_dma_std_transfer_type(emDMA dma, uint8_t ch, emDMATRTYPE t)
{
    dma_set_transfer_type((dma_idx_t)dma, ch, (dma_tr_type_t)t);
}
static inline int set_dma_handshaking(emDMA dma, uint8_t ch, uint16_t s, uint16_t d)
{
    return dma_set_handshaking((dma_idx_t)dma, ch, s, d);
}
static inline void set_dma_std_increment(emDMA dma, uint8_t ch, emDMAINCDIR s, emDMAINCDIR d)
{
    dma_set_increment((dma_idx_t)dma, ch, (dma_inc_t)s, (dma_inc_t)d);
}
static inline void set_dma_burst_size(emDMA dma, uint8_t ch, emDMAMSIZE s, emDMAMSIZE d)
{
    dma_set_burst((dma_idx_t)dma, ch, (dma_msize_t)s, (dma_msize_t)d);
}
static inline void set_dma_std_transfer_bitwidth(emDMA dma, uint8_t ch, emDMATRWIDTH s, emDMATRWIDTH d)
{
    dma_set_width((dma_idx_t)dma, ch, (dma_width_t)s, (dma_width_t)d);
}
static inline void set_dma_interrupt(emDMA dma, uint8_t ch, emDMAIntType type, int en)
{
    dma_set_interrupt((dma_idx_t)dma, ch, (dma_int_t)type, en != 0);
}
static inline int is_dma_busy(emDMA dma, uint8_t ch)
{
    return dma_is_busy((dma_idx_t)dma, ch);
}
static inline void set_dma_reload(emDMA dma, uint8_t ch, int sr, int dr)
{
    dma_set_reload((dma_idx_t)dma, ch, sr != 0, dr != 0);
}
static inline void set_dma_stop(emDMA dma, uint8_t ch)
{
    dma_stop((dma_idx_t)dma, ch);
}
static inline void set_dma_std_address(emDMA dma, uint8_t ch, uint32_t s, uint32_t d)
{
    dma_set_address((dma_idx_t)dma, ch, s, d);
}

/* Legacy-friendly wrappers for scatter/gather */
static inline void set_dma_src_gather(emDMA dma, uint8_t ch, uint32_t count, uint32_t interval)
{
    dma_set_source_gather((dma_idx_t)dma, ch, count, interval);
}
static inline void set_dma_dst_scatter(emDMA dma, uint8_t ch, uint32_t count, uint32_t interval)
{
    dma_set_dest_scatter((dma_idx_t)dma, ch, count, interval);
}

/* Legacy-friendly wrappers for LLI memcpy */
static inline uint32_t calc_dma_lli_count(uint32_t len, emDMATRWIDTH w)
{
    return dma_calc_lli_count(len, (dma_width_t)w);
}
static inline int set_dma_memcpy_lli(emDMA dma, uint8_t ch, uint32_t src, uint32_t dst, uint32_t len,
                                     emDMATRWIDTH w, dma_lli_t *llis, uint32_t lli_capacity)
{
    return dma_memcpy_lli((dma_idx_t)dma, ch, src, dst, len, (dma_width_t)w, llis, lli_capacity);
}
static inline int set_dma_memcpy_lli_blocking(emDMA dma, uint8_t ch, uint32_t src, uint32_t dst, uint32_t len,
                                              emDMATRWIDTH w, dma_lli_t *llis, uint32_t lli_capacity)
{
    return dma_memcpy_lli_blocking((dma_idx_t)dma, ch, src, dst, len, (dma_width_t)w, llis, lli_capacity);
}

/* Legacy handshake IDs mapped to CFG_H src/dst peripheral select. Keep numeric values consistent with legacy reference. */
#ifndef EM_HAND_DEFS
#define EM_HAND_DEFS
enum {
    EM_HAND_UART0_RX = 0x00,
    EM_HAND_UART0_TX,
    EM_HAND_UART1_RX,
    EM_HAND_UART1_TX,
    EM_HAND_UART2_RX,
    EM_HAND_UART2_TX,
    EM_HAND_UART3_RX,
    EM_HAND_UART3_TX,
    EM_HAND_SPI0_RX,
    EM_HAND_SPI0_TX,
    EM_HAND_SPI1_RX,
    EM_HAND_SPI1_TX,
    EM_HAND_I2S0_RX,
    EM_HAND_I2S0_TX,
    EM_HAND_I2S1_RX,
    EM_HAND_I2S1_TX,
    EM_HAND_PDM = 0x100,
    EM_HAND_AON,
    EM_HAND_NULL = 0xFFFF,
};
#endif /* EM_HAND_DEFS */

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_DMA_H */
