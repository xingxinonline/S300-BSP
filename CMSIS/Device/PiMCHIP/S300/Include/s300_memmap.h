#ifndef PIMCHIP_S300_MEMMAP_H
#define PIMCHIP_S300_MEMMAP_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CMSIS-style base addresses for PiMCHIP S300
 * Migrated from cortex-m4-i2s/include/register.h (CONFIG_SW_S300)
 */

/* -------------------- BUS MATRIX (Slave views) -------------------- */
#define M4_SLV_ROM_BASE              (0x00000000UL)
#define M4_SLV_ROM_END               (0x000027FFUL)
#define M4_SLV_RAM0_BASE             (0x10000000UL)
#define M4_SLV_RAM0_END              (0x10001FFFUL)
#define M4_SLV_RAM1_BASE             (0x20000000UL)
#define M4_SLV_RAM1_END              (0x2005FFFFUL)
#define M4_SLV_APB0_BASE             (0x40000000UL)
#define M4_SLV_APB0_END              (0x4000FFFFUL)
#define M4_SLV_APB1_BASE             (0x40010000UL)
#define M4_SLV_APB1_END              (0x4001FFFFUL)
#define M4_SLV_AHB_SLV_BASE          (0x41000000UL)
#define M4_SLV_AHB_SLV_END           (0x41FFFFFFUL)
#define M4_SLV_FLASH_BASE            (0x08000000UL)
#define M4_SLV_FLASH_END             (0x09FFFFFFUL)
#define M4_SLV_PSRAM_BASE            (0x80000000UL)
#define M4_SLV_PSRAM_END             (0x81FFFFFFUL)
#define M4_SLV_AUDIO_BASE            (0x42000000UL)
#define M4_SLV_AUDIO_END             (0x4200FFFFUL)
#define M4_SLV_AON_BASE              (0x43000000UL)
#define M4_SLV_AON_END               (0x4302FFFFUL)
#define M4_SLV_DSP_BASE              (0x44000000UL)
#define M4_SLV_DSP_END               (0x44FFFFFFUL)

/* -------------------- APB0 -------------------- */
#define TIME0_BASE                   (0x40000000UL)
#define TIME0_END                    (0x40000FFFUL)
#define TIME1_BASE                   (0x40001000UL)
#define TIME1_END                    (0x40001FFFUL)
#define TIME2_BASE                   (0x40002000UL)
#define TIME2_END                    (0x40002FFFUL)
#define WDT0_BASE                    (0x40003000UL)
#define WDT0_END                     (0x40003FFFUL)
#define WDT1_BASE                    (0x40004000UL)
#define WDT1_END                     (0x40004FFFUL)
#define WDT2_BASE                    (0x40005000UL)
#define WDT2_END                     (0x40005FFFUL)
#define WDT3_BASE                    (0x40006000UL)
#define WDT3_END                     (0x40006FFFUL)
#define INT_CTRL_BASE                (0x40007000UL)
#define INT_CTRL_END                 (0x40007FFFUL)
#define IO_MATRIX_BASE               (0x40008000UL)
#define IO_MATRIX_END                (0x40008FFFUL)
#define IO_MUX_BASE                  (0x40009000UL)
#define IO_MUX_END                   (0x40009FFFUL)
#define RCC_BASE                     (0x4000A000UL)
#define RCC_END                      (0x4000AFFFUL)
#define SEC_BASE                     (0x4000B000UL)
#define SEC_END                      (0x4000BFFFUL)
#define SCTRL_BASE                   (0x4000C000UL)
#define SCTRL_END                    (0x4000CFFFUL)
#define QSPI_CFG_BASE                (0x4000D000UL)
#define QSPI_CFG_END                 (0x4000DFFFUL)
#define SECOTP1_BASE                 (0x4000E000UL)
#define SECOTP1_END                  (0x4000EFFFUL)
#define SECOTP2_BASE                 (0x4000F000UL)
#define SECOTP2_END                  (0x4000FFFFUL)

/* Security sub-blocks (aliases under SEC_BASE) */
#define DUBHE_CTRL_BASE              (0x4000B000UL)
#define DUBHE_STAT_BASE              (0x4000B100UL)
#define TRNG_BASE                    (0x4000B200UL)
#define TRNG_TOOL_BASE               (0x4000B280UL)
#define DBG_CTRL_BASE                (0x4000B300UL)
#define OTP_MGR_BASE                 (0x4000B400UL)
#define DUBHE_REV_BASE               (0x4000B500UL)
#define ACA_Q0_BASE                  (0x4000B600UL)
#define SCA_Q0_BASE                  (0x4000B800UL)
#define HASH_Q0_BASE                 (0x4000B880UL)
#define Q0_DBG_BASE                  (0x4000B900UL)
#define ACA_Q1_BASE                  (0x4000BA00UL)
#define SCA_Q1_BASE                  (0x4000BC00UL)
#define HASH_Q1_BASE                 (0x4000BC80UL)
#define Q1_DBG_BASE                  (0x4000BD00UL)

/* -------------------- APB1 -------------------- */
#define UART0_BASE                   (0x40010000UL)
#define UART1_BASE                   (0x40011000UL)
#define UART2_BASE                   (0x40012000UL)
#define UART3_BASE                   (0x40013000UL)
#define I2C0_BASE                    (0x40014000UL)
#define I2C1_BASE                    (0x40015000UL)
#define I2C2_BASE                    (0x40016000UL)
#define I2C3_BASE                    (0x40017000UL)
#define GPIO_BASE                    (0x40018000UL)
#define MAILBOX_BASE                 (0x40019000UL)
#define I2S0_BASE                    (0x4001B000UL)
#define I2S1_BASE                    (0x4001C000UL)
#define PWM_BASE                     (0x4001D000UL)
#define DVP_BASE                     (0x4001F000UL)

/* -------------------- AHB -------------------- */
#define DMA0_BASE                    (0x41000000UL)
#define DMA1_BASE                    (0x41100000UL)
#define SPI0_BASE                    (0x41800000UL)
#define SPI1_BASE                    (0x41900000UL)
#define AHB_DVP_BASE                 (0x41A00000UL)
#define AHB_DVP1_BASE                (0x41B00000UL)
#define SDIO0_BASE                   (0x41C00000UL)
#define SDIO1_BASE                   (0x41D00000UL)
#define ETH_BASE                     (0x41200000UL)

/* -------------------- AON -------------------- */
#define AON_DMA_BASE                 (0x43000000UL)
#define AON_VPROC_BASE               (0x43004000UL)
#define AON_APROC_BASE               (0x43008000UL)
#define AON_I2S0_BASE                (0x43010000UL)
#define AON_I2S1_BASE                (0x43011000UL)
#define AON_SPI0_BASE                (0x43012000UL)
#define AON_SPI1_BASE                (0x43013000UL)
#define AON_GPIO_BASE                (0x43014000UL)
#define AON_WDOG_BASE                (0x43015000UL)
#define AON_TIMER_BASE               (0x43016000UL)
#define AON_RTC_BASE                 (0x43017000UL)
#define AON_CFG_BASE                 (0x43018000UL)
#define AON_SRAM1_BASE               (0x43020000UL)
#define AON_SRAM0_BASE               (0x43024000UL)

/* -------------------- DSP -------------------- */
#define DSP_SRAM0_BASE               (0x44000000UL)
#define DSP_SRAM1_BASE               (0x44040000UL)
#define DSP_RCC_BASE                 (0x44080000UL)
#define DSP_MAILBOX_BASE             (0x44080400UL)
#define DSP_SYSCTL_BASE              (0x44080800UL)
#define DSP_VIDEO_SS_BASE            (0x44080C00UL)
#define DSP_AXI_BUS_CFG_BASE         (0x44081000UL)
#define DSP_NPU_BASE                 (0x44081400UL)
#define DSP_EDAP_BASE                (0x44800000UL)
#define DSP_DTCM_BASE                (0x44800000UL)
#define DSP_PTCM_BASE                (0x44A00000UL)
#define DSP_AXI_DMA_BASE             (0x44088000UL)
#define DSP_PIM_AHB_BASE             (0x4408A000UL)
#define DSP_PIM_BASE                 (0x60000000UL)
#define DSP_PSRAM_BASE               (0x80000000UL)

#ifdef __cplusplus
}
#endif

#endif /* PIMCHIP_S300_MEMMAP_H */
