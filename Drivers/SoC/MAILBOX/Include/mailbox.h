#ifndef S300_BSP_MAILBOX_H
#define S300_BSP_MAILBOX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "s300.h"
#include "mailbox_s300.h"

/*
 Public API for MAILBOX peripheral

 Notes on semantics:
 - Write path returns 0 on success, -1 on timeout when TX FIFO is full.
 - Read path returns 0 on success with data via pointer, -1 on timeout when RX FIFO is empty.
 - Thresholds are raw FIFO levels (0..255) as per HW doc.
 - Interrupt enable mask uses bit0=SIT, bit1=RIT, bit2=ERR (compatible with reference code).
*/

typedef enum
{
    MAILBOX_IRQ_NONE         = 0,
    MAILBOX_IRQ_SIT_EN       = (1u << 0),
    MAILBOX_IRQ_RIT_EN       = (1u << 1),
    MAILBOX_IRQ_ERR_EN       = (1u << 2),
    MAILBOX_IRQ_ALL          = (MAILBOX_IRQ_SIT_EN | MAILBOX_IRQ_RIT_EN | MAILBOX_IRQ_ERR_EN),
} mailbox_irq_mask_t;

/* Core operations */
int mailbox_init(uint32_t base, uint8_t thresh, uint32_t irq_en_mask);
int mailbox_write_u32(uint32_t base, uint32_t data, uint32_t timeout);
int mailbox_read_u32(uint32_t base, uint32_t *out, uint32_t timeout);
int mailbox_clear_fifo(uint32_t base, uint32_t timeout);

/* Low-level configuration helpers (mirrors reference names) */
void mailbox_set_sit(uint32_t base, uint8_t thresh);
void mailbox_set_rit(uint32_t base, uint8_t thresh);
void mailbox_set_is(uint32_t base, uint8_t bits);   /* write-1-to-clear */
void mailbox_set_ie(uint32_t base, uint8_t mask);
void mailbox_set_ctrl(uint32_t base, uint8_t mask); /* bit0:clr TX, bit1:clr RX */

/* Status/diagnostic helpers (return 0=OK, <0 indicates mismatch) */
int mailbox_err_full_flag_is(uint32_t base, uint8_t expect);
int mailbox_err_empty_flag_is(uint32_t base, uint8_t expect);
int mailbox_sta_full_flag_is(uint32_t base, uint8_t expect);
int mailbox_sta_empty_flag_is(uint32_t base, uint8_t expect);
int mailbox_sta_tx_le_thresh(uint32_t base);   /* expect TX level <= SIT */
int mailbox_sta_tx_gt_thresh(uint32_t base);   /* expect TX level >  SIT */
int mailbox_sta_rx_le_thresh(uint32_t base);   /* expect RX level <= RIT */
int mailbox_sta_rx_gt_thresh(uint32_t base);   /* expect RX level >  RIT */

/* Convenience combos */
int mailbox_tx_all_normal(uint32_t base);
int mailbox_rx_all_normal(uint32_t base);

/* Convenience data transfer helpers (optional) */
void mailbox_write_loop(uint32_t base, const uint32_t *data, uint32_t dlen);
void mailbox_read_loop(uint32_t base, uint32_t dlen);
int  m4_dsp_one_data(uint32_t data);
int  m4_dsp_loop(const uint32_t *data, uint32_t dlen);

/* Legacy-compatible inline aliases to ease porting existing code from docs */
static inline int init_mailbox(uint32_t base, uint8_t thresh, uint32_t enisr)
{
    return mailbox_init(base, thresh, enisr);
}
static inline int write_mailbox(uint32_t base, uint32_t data)
{
    return mailbox_write_u32(base, data, 1000);
}
static inline uint32_t read_mailbox(uint32_t base)
{
    uint32_t val = 0u;
    return (mailbox_read_u32(base, &val, 1000) == 0) ? val : (uint32_t)0;
}
static inline void set_sit(uint32_t base, uint8_t thresh)
{
    mailbox_set_sit(base, thresh);
}
static inline void set_rit(uint32_t base, uint8_t thresh)
{
    mailbox_set_rit(base, thresh);
}
static inline void set_is(uint32_t base, uint8_t num)
{
    mailbox_set_is(base, num);
}
static inline void set_ie(uint32_t base, uint8_t num)
{
    mailbox_set_ie(base, num);
}
static inline void set_ctrl(uint32_t base, uint8_t num)
{
    mailbox_set_ctrl(base, num);
}
static inline int clear_fifo(uint32_t base)
{
    return mailbox_clear_fifo(base, 1000);
}

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_MAILBOX_H */
