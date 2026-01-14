#include <stdint.h>
#include "mailbox.h"

/* Internal helpers */
static inline S300_MAILBOX_TypeDef *mbx_from_base(uint32_t base)
{
    return (S300_MAILBOX_TypeDef *)(uintptr_t)base;
}

int mailbox_init(uint32_t base, uint8_t thresh, uint32_t irq_en_mask)
{
    /* Clear FIFOs */
    (void)mailbox_clear_fifo(base, 1000);

    /* Disable all IRQs while configuring */
    mailbox_set_ie(base, 0);

    /* Clear any pending interrupts (ERR/RIT/SIT) */
    mailbox_set_is(base, 0x7u);

    /* Configure thresholds */
    mailbox_set_sit(base, thresh);
    mailbox_set_rit(base, thresh);

    /* Enable requested IRQ mask */
    mailbox_set_ie(base, (uint8_t)(irq_en_mask & 0x7u));

    /* Final FIFO clear just in case */
    (void)mailbox_clear_fifo(base, 1000);
    return 0;
}

int mailbox_write_u32(uint32_t base, uint32_t data, uint32_t timeout)
{
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    while ((m->STATUS & MAILBOX_STATUS_FULL_Msk) && (timeout--)) {}
    if (timeout == 0)
        return -1;
    m->WRDATA = data;
    return 0;
}

int mailbox_read_u32(uint32_t base, uint32_t *out, uint32_t timeout)
{
    if (!out) return -2;
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    while ((m->STATUS & MAILBOX_STATUS_EMPTY_Msk) && (timeout--)) {}
    if (timeout == 0)
        return -1;
    *out = m->RDDATA;
    return 0;
}

int mailbox_clear_fifo(uint32_t base, uint32_t timeout)
{
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    /* Write 1 to clear both TX (bit0) and RX (bit1) FIFOs */
    m->CTRL = (MAILBOX_CTRL_CLR_TX_Msk | MAILBOX_CTRL_CLR_RX_Msk);

    /* Wait error bits clear (bit0 empty error, bit1 full error) */
    while ((m->ERROR & (MAILBOX_ERR_EMPTY_Msk)) && (timeout--)) {}
    if (timeout == 0) return -1;

    while ((m->ERROR & (MAILBOX_ERR_FULL_Msk)) && (timeout--)) {}
    if (timeout == 0) return -1;
    return 0;
}

void mailbox_set_sit(uint32_t base, uint8_t thresh)
{
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    uint32_t t = m->SIT;
    t &= ~0xFFu;
    t |= ((uint32_t)thresh & 0xFFu);
    m->SIT = t;
}

void mailbox_set_rit(uint32_t base, uint8_t thresh)
{
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    uint32_t t = m->RIT;
    t &= ~0xFFu;
    t |= ((uint32_t)thresh & 0xFFu);
    m->RIT = t;
}

void mailbox_set_is(uint32_t base, uint8_t bits)
{
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    /* write-1-to-clear on bits [2:0] */
    uint32_t val = m->IS;
    val &= ~0x7u;
    val |= (bits & 0x7u);
    m->IS = val;
}

void mailbox_set_ie(uint32_t base, uint8_t mask)
{
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    uint32_t val = m->IE;
    val &= ~0x7u;
    val |= (mask & 0x7u);
    m->IE = val;
}

void mailbox_set_ctrl(uint32_t base, uint8_t mask)
{
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    uint32_t val = m->CTRL;
    val &= ~0x3u;
    val |= (mask & 0x3u);
    m->CTRL = val;
}

/* Diagnostic helpers */
int mailbox_err_full_flag_is(uint32_t base, uint8_t expect)
{
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    uint32_t bit = (m->ERROR >> MAILBOX_ERR_FULL_Pos) & 0x1u;
    return (bit == (expect & 1u)) ? 0 : -1;
}

int mailbox_err_empty_flag_is(uint32_t base, uint8_t expect)
{
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    uint32_t bit = (m->ERROR >> MAILBOX_ERR_EMPTY_Pos) & 0x1u;
    return (bit == (expect & 1u)) ? 0 : -1;
}

int mailbox_sta_full_flag_is(uint32_t base, uint8_t expect)
{
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    uint32_t bit = (m->STATUS >> MAILBOX_STATUS_FULL_Pos) & 0x1u;
    return (bit == (expect & 1u)) ? 0 : -1;
}

int mailbox_sta_empty_flag_is(uint32_t base, uint8_t expect)
{
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    uint32_t bit = (m->STATUS >> MAILBOX_STATUS_EMPTY_Pos) & 0x1u;
    return (bit == (expect & 1u)) ? 0 : -1;
}

int mailbox_sta_tx_le_thresh(uint32_t base)
{
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    /* Expect TXTA bit == 1 when TX level <= SIT */
    return ((m->STATUS & MAILBOX_STATUS_TXTA_Msk) ? 0 : -1);
}

int mailbox_sta_tx_gt_thresh(uint32_t base)
{
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    /* Expect TXTA bit == 0 when TX level > SIT */
    return ((m->STATUS & MAILBOX_STATUS_TXTA_Msk) ? -1 : 0);
}

int mailbox_sta_rx_le_thresh(uint32_t base)
{
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    /* Expect RXTA bit == 0 when RX level <= RIT */
    return ((m->STATUS & MAILBOX_STATUS_RXTA_Msk) ? -1 : 0);
}

int mailbox_sta_rx_gt_thresh(uint32_t base)
{
    S300_MAILBOX_TypeDef *m = mbx_from_base(base);
    /* Expect RXTA bit == 1 when RX level > RIT */
    return ((m->STATUS & MAILBOX_STATUS_RXTA_Msk) ? 0 : -1);
}

int mailbox_tx_all_normal(uint32_t base)
{
    if (mailbox_err_full_flag_is(base, 0) != 0) return -1;
    if (mailbox_sta_full_flag_is(base, 0) != 0) return -2;
    if (mailbox_sta_tx_le_thresh(base) != 0)    return -3;
    return 0;
}

int mailbox_rx_all_normal(uint32_t base)
{
    if (mailbox_err_empty_flag_is(base, 0) != 0) return -1;
    if (mailbox_sta_empty_flag_is(base, 0) != 0) return -2;
    if (mailbox_sta_rx_le_thresh(base) != 0)    return -3;
    return 0;
}

void mailbox_write_loop(uint32_t base, const uint32_t *data, uint32_t dlen)
{
    if (!data) return;
    for (uint32_t i = 0; i < dlen; ++i)
    {
        (void)mailbox_write_u32(base, data[i], 1000);
    }
}

void mailbox_read_loop(uint32_t base, uint32_t dlen)
{
    uint32_t tmp = 0;
    for (uint32_t i = 0; i < dlen; ++i)
    {
        (void)mailbox_read_u32(base, &tmp, 1000);
    }
    (void)tmp;
}

int m4_dsp_one_data(uint32_t data)
{
    uint32_t tmp = 0;
    if (mailbox_write_u32(MAILBOX_BASE, data, 1000) != 0) return -1;
    if (mailbox_read_u32(DSP_MAILBOX_BASE, &tmp, 1000) != 0) return -1;
    if (tmp != data) return -1;

    if (mailbox_write_u32(DSP_MAILBOX_BASE, data, 1000) != 0) return -2;
    if (mailbox_read_u32(MAILBOX_BASE, &tmp, 1000) != 0) return -2;
    if (tmp != data) return -2;
    return 0;
}

int m4_dsp_loop(const uint32_t *data, uint32_t dlen)
{
    if (!data) return -1;
    uint32_t tmp = 0;
    for (uint32_t i = 0; i < dlen; ++i)
    {
        if (mailbox_write_u32(MAILBOX_BASE, data[i], 1000) != 0) return -1;
    }
    for (uint32_t i = 0; i < dlen; ++i)
    {
        if (mailbox_read_u32(DSP_MAILBOX_BASE, &tmp, 1000) != 0) return -1;
        if (tmp != data[i]) return -1;
    }

    for (uint32_t i = 0; i < dlen; ++i)
    {
        if (mailbox_write_u32(DSP_MAILBOX_BASE, data[i], 1000) != 0) return -2;
    }
    for (uint32_t i = 0; i < dlen; ++i)
    {
        if (mailbox_read_u32(MAILBOX_BASE, &tmp, 1000) != 0) return -2;
        if (tmp != data[i]) return -2;
    }
    return 0;
}
