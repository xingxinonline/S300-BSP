#ifndef _PSRAM_H_
#define _PSRAM_H_
#ifdef __cplusplus
 extern "C" {
#endif 
#include "s300.h"
//DWC_ssi register address block - 1 ssic_address_block  Exists : SSIC_SPI_BRIDGE == 0
#ifndef CONFIG_SSIC_SPI_BRIDGE
#define PSRAM_SSI_CTRLR0					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0000)))
#define PSRAM_SSI_CTRLR1					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0004)))
#define PSRAM_SSI_SSIENR					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0008)))
#define PSRAM_SSI_MWCR					    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x000C)))
#define PSRAM_SSI_SER						(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0010)))
#define PSRAM_SSI_BAUDR					    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0014)))
#define PSRAM_SSI_TXFTLR					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0018)))
#define PSRAM_SSI_RXFTLR					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x001C)))
#define PSRAM_SSI_TXFLR					    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0020)))
#define PSRAM_SSI_RXFLR					    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0024)))
#define PSRAM_SSI_SR						(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0028)))
#define PSRAM_SSI_IMR						(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x002C)))
#define PSRAM_SSI_ISR						(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0030)))
#define PSRAM_SSI_RISR					    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0034)))
#define PSRAM_SSI_TXEICR					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0038)))
#define PSRAM_SSI_RXOICR					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x003c)))
#define PSRAM_SSI_RXUICR					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0040)))
#define PSRAM_SSI_MSTICR					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0044)))
#define PSRAM_SSI_ICR						(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0048)))
#define PSRAM_SSI_DMACR					    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x004c)))
#define PSRAM_SSI_DMATDLR					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0050)))
#define PSRAM_SSI_AXIAWLEN				    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0050)))
#define PSRAM_SSI_DMARDLR					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0054)))
#define PSRAM_SSI_AXIARLEN				    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0054)))
#define PSRAM_SSI_IDR						(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0058)))
#define PSRAM_SSI_SSIC_VERSION_ID			(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x005c)))
#define PSRAM_SSI_DR(n)					    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + (n)*0x0004 + 0x0060)))
#define PSRAM_SSI_RX_SAMPLE_DELAY			(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x00f0)))
#define PSRAM_SSI_SPI_CTRLR0				(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x00f4)))
#define PSRAM_SSI_DDR_DRIVE_EDGE			(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x00f8)))
#define PSRAM_SSI_XIP_MODE_BITS			    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x00fc)))
#endif
//DWC_ssi register address block 2 ssic_address_block2 Exists : SSIC_SPI_BRIDGE == 0
#ifndef CONFIG_SSIC_SPI_BRIDGE
#define PSRAM_SSI_XIP_INCR_INST			    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0100)))
#define PSRAM_SSI_XIP_WRAP_INST		        (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0104)))
#define PSRAM_SSI_XIP_CTRL				    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0108)))
#define PSRAM_SSI_XIP_SER					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x010C)))
#define PSRAM_SSI_XRXOICR					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0110)))
#define PSRAM_SSI_XIP_CNT_TIME_OUT		    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0114)))
#define PSRAM_SSI_SPI_CTRLR1				(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0118)))
#define PSRAM_SSI_SPITECR					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x011C)))
#define PSRAM_SSI_SPIDR					    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0120)))
#define PSRAM_SSI_SPIAR					    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0124)))
#define PSRAM_SSI_AXIAR0					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0128)))
#define PSRAM_SSI_AXIAR1					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x012C)))
#define PSRAM_SSI_AXIECR					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0130)))
#define PSRAM_SSI_DONECR					(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0134)))
#define PSRAM_SSI_XIP_WRITE_INCR_INST		(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0140)))
#define PSRAM_SSI_XIP_WRITE_WRAP_INST  	    (*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0144)))
#define PSRAM_SSI_XIP_WRITE_CTRL			(*((volatile uint32_t *)(M4_SLV_PSRAM_BASE + 0x0000 + 0x0148)))
#endif

#define PSRAM_BUSY             0x01
#define PSRAM_TFNF             0x02
#define PSRAM_TFE              0x04
#define PSRAM_RFNE             0x08
#define PSRAM_RFF              0x10
#define PSRAM_FIFO_MAX_LV      16


#define DSP_SCTRL_CFG5                     (*((volatile uint32_t*)(DSP_SYSCTL_BASE + 0x0014)))
#define DSP_SCTRL_CFG5_XIP_EN_MASK         (1U << 0)
#define DSP_SCTRL_CFG5_DLYSEL_MASK         (0xffU << 8)
#define DSP_SCTRL_CFG5_PSRAM_DLYSEL_SHIFT  8U
#define DSP_SCTRL_CFG5_PSRAM_DLYSEL_DEFAULT 0x0aU
#define PSRAM_XIP_DIS                      DSP_SCTRL_CFG5 &= ~DSP_SCTRL_CFG5_XIP_EN_MASK
#define PSRAM_XIP_EN                       DSP_SCTRL_CFG5 |= DSP_SCTRL_CFG5_XIP_EN_MASK

int init_psram(uint32_t ahbclock, uint32_t psramclock);
void set_psram_state(void);
#endif //CONFIG_PSRAM_MODULE


#ifdef __cplusplus
}
#endif
