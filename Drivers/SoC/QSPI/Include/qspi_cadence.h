#ifndef S300_BSP_QSPI_CADENCE_H
#define S300_BSP_QSPI_CADENCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "s300_memmap.h"

/* Cadence QSPI APB register map (minimal subset) */

/* Octal-SPI Configuration Register */
#define CQSPI_REG_CONFIG                 0x00u
#define   CQSPI_CFG_ENABLE               (1u << 0)   /* Octal-SPI Enable */
#define   CQSPI_CFG_CLK_POL              (1u << 1)   /* Clock polarity */
#define   CQSPI_CFG_CLK_PHA              (1u << 2)   /* Clock phase */
#define   CQSPI_CFG_PHY_MODE             (1u << 3)   /* PHY Mode enable */
#define   CQSPI_CFG_HOLD                 (1u << 4)   /* Set to drive the HOLD pin */
#define   CQSPI_CFG_RESET                (1u << 5)   /* Set to drive the RESET pin */
#define   CQSPI_CFG_RESET_CFG            (1u << 6)   /* RESET pin configuration */
#define   CQSPI_CFG_DIRECT               (1u << 7)   /* Enable Direct Access Controller */
#define   CQSPI_CFG_LEGACY               (1u << 8)   /* Legacy IP Mode Enable */
#define   CQSPI_CFG_DECODE               (1u << 9)   /* Peripheral select decode */
#define   CQSPI_CFG_CHIPSELECT_LSB       10u         /* Peripheral chip select lines */
#define   CQSPI_CFG_CHIPSELECT_MASK      0xFu
#define   CQSPI_CFG_WP                   (1u << 14)  /* Set to drive the Write Protect pin */
#define   CQSPI_CFG_DMA                  (1u << 15)  /* Enable DMA Peripheral Interface */
#define   CQSPI_CFG_REMAP                (1u << 16)  /* Enable AHB Address Re-mapping */
#define   CQSPI_CFG_XIP_NEXT             (1u << 17)  /* Enter XIP Mode on next READ */
#define   CQSPI_CFG_XIP_IMM              (1u << 18)  /* Enter XIP Mode immediately */
#define   CQSPI_CFG_BAUD_LSB             19u         /* Master mode baud rate divisor */
#define   CQSPI_CFG_BAUD_MASK            0xFu
#define   CQSPI_CFG_AHB_DEC              (1u << 23)  /* Enable AHB Decoder */
#define   CQSPI_CFG_DTR                  (1u << 24)  /* Enable DTR Protocol */
#define   CQSPI_CFG_PIPE_PHY             (1u << 25)  /* Pipeline PHY Mode enable */
#define   CQSPI_CFG_CRC                  (1u << 29)  /* CRC enable bit */
#define   CQSPI_CFG_DUAL_OPCODE          (1u << 30)  /* Dual-byte Opcode Mode enable */
#define   CQSPI_CFG_IDLE                 (1u << 31)  /* Serial Interface and Octal-SPI pipeline is IDLE */
#define   CQSPI_CFG_IDLE_LSB             31u         /* IDLE bit position (added for wait polling) */

/* Device Read Instruction Register */
#define CQSPI_REG_RD_INSTR               0x04u
#define   CQSPI_RD_OPCODE_LSB            0u          /* Read Opcode */
#define   CQSPI_RD_OPCODE_MASK           0xFFu
#define   CQSPI_RD_TYPE_INSTR_LSB        8u          /* Instruction Type */
#define   CQSPI_RD_TYPE_INSTR_MASK       0x3u
#define   CQSPI_RD_DDR_EN                (1u << 10)  /* DDR Bit Enable */
#define   CQSPI_RD_PREDICT_DIS           (1u << 11)  /* Predicted Read Disable Bit */
#define   CQSPI_RD_TYPE_ADDR_LSB         12u         /* Address Transfer Type */
#define   CQSPI_RD_TYPE_ADDR_MASK        0x3u
#define   CQSPI_RD_TYPE_DATA_LSB         16u         /* Data Transfer Type */
#define   CQSPI_RD_TYPE_DATA_MASK        0x3u
#define   CQSPI_RD_MODE_EN_LSB           20u         /* Mode Bit Enable */
#define   CQSPI_RD_DUMMY_LSB             24u         /* Number of Dummy Clock Cycles */
#define   CQSPI_RD_DUMMY_MASK            0x1Fu

/* Device Write Instruction Register */
#define CQSPI_REG_WR_INSTR               0x08u
#define   CQSPI_WR_OPCODE_LSB            0u          /* Write Opcode */
#define   CQSPI_WR_OPCODE_MASK           0xFFu
#define   CQSPI_WR_WEL_DIS               (1u << 8)   /* WEL Disable */
#define   CQSPI_WR_TYPE_ADDR_LSB         12u         /* Address Transfer Type */
#define   CQSPI_WR_TYPE_ADDR_MASK        0x3u
#define   CQSPI_WR_TYPE_DATA_LSB         16u         /* Data Transfer Type */
#define   CQSPI_WR_TYPE_DATA_MASK        0x3u
#define   CQSPI_WR_DUMMY_LSB             24u         /* Number of Dummy Clock Cycles */
#define   CQSPI_WR_DUMMY_MASK            0x1Fu

/* Device Delay Register */
#define CQSPI_REG_DELAY                  0x0Cu
#define   CQSPI_DELAY_TSLCH_LSB          0u          /* CSSOT - Chip Select Start Of Transfer */
#define   CQSPI_DELAY_TSLCH_MASK         0xFFu
#define   CQSPI_DELAY_TCHSH_LSB          8u          /* CSEOT - Chip Select End Of Transfer */
#define   CQSPI_DELAY_TCHSH_MASK         0xFFu
#define   CQSPI_DELAY_TSD2D_LSB          16u         /* CSDADS - Chip Select De-Assert Different Slaves */
#define   CQSPI_DELAY_TSD2D_MASK         0xFFu
#define   CQSPI_DELAY_TSHSL_LSB          24u         /* CSDA - Chip Select De-Assert */
#define   CQSPI_DELAY_TSHSL_MASK         0xFFu

/* Read Data Capture Register */
#define CQSPI_REG_RD_DATA_CAPTURE        0x10u
#define   CQSPI_RD_CAPTURE_BYPASS        (1u << 0)   /* Bypass of the adapted loopback clock circuit */
#define   CQSPI_RD_CAPTURE_DELAY_LSB     1u          /* Delay the read data capturing logic */
#define   CQSPI_RD_CAPTURE_DELAY_MASK    0xFu
#define   CQSPI_RD_CAPTURE_SAMPLE_EDGE   (1u << 5)   /* Sample edge selection */
#define   CQSPI_RD_CAPTURE_DQS_EN        (1u << 8)   /* DQS enable bit */
#define   CQSPI_RD_CAPTURE_TX_DELAY_LSB  16u         /* Delay the transmitted data */
#define   CQSPI_RD_CAPTURE_TX_DELAY_MASK 0xFu

/* Device Size Configuration Register */
#define CQSPI_REG_SIZE                   0x14u
#define   CQSPI_SIZE_ADDR_LSB            0u          /* Number of address bytes */
#define   CQSPI_SIZE_ADDR_MASK           0xFu
#define   CQSPI_SIZE_PAGE_LSB            4u          /* Number of bytes per device page */
#define   CQSPI_SIZE_PAGE_MASK           0xFFFu
#define   CQSPI_SIZE_BLOCK_LSB           16u         /* Number of bytes per block */
#define   CQSPI_SIZE_BLOCK_MASK          0x1Fu
#define   CQSPI_SIZE_CS0_LSB             21u         /* Size of Flash Device connected to CS[0] pin */
#define   CQSPI_SIZE_CS0_MASK            0x3u
#define   CQSPI_SIZE_CS1_LSB             23u         /* Size of Flash Device connected to CS[1] pin */
#define   CQSPI_SIZE_CS1_MASK            0x3u
#define   CQSPI_SIZE_CS2_LSB             25u         /* Size of Flash Device connected to CS[2] pin */
#define   CQSPI_SIZE_CS2_MASK            0x3u
#define   CQSPI_SIZE_CS3_LSB             27u         /* Size of Flash Device connected to CS[3] pin */
#define   CQSPI_SIZE_CS3_MASK            0x3u

/* SRAM Partition Configuration Register */
#define CQSPI_REG_SRAMPARTITION          0x18u
#define   CQSPI_SRAM_PARTITION_LSB       0u          /* Size of the indirect read partition in the SRAM */
/* SRAM depth bits N: from integration; default N=8 (2**8 = 256 locations, 32-bit each) */
#ifndef CQSPI_SRAM_DEPTH_N
#define CQSPI_SRAM_DEPTH_N               8u
#endif
#define   CQSPI_SRAM_PARTITION_MASK      ((1u << CQSPI_SRAM_DEPTH_N) - 1u)
#define   CQSPI_SRAM_TOTAL_LOCATIONS     (1u << CQSPI_SRAM_DEPTH_N)

/* Indirect AHB Address Trigger Register */
#define CQSPI_REG_INDIRECTTRIGGER        0x1Cu
#define   CQSPI_INDIRECT_TRIGGER_ADDR_LSB 0u          /* Indirect Trigger Address */
#define   CQSPI_INDIRECT_TRIGGER_ADDR_MASK 0xFFFFFFFFu

/* DMA Peripheral Configuration Register */
#define CQSPI_REG_DMAPERIPH              0x20u
#define   CQSPI_DMA_SINGLE_LSB           0u          /* Number of bytes in a single type request */
#define   CQSPI_DMA_SINGLE_MASK          0xFu
#define   CQSPI_DMA_BURST_LSB            8u          /* Number of bytes in a burst type request */
#define   CQSPI_DMA_BURST_MASK           0xFu

/* Remap Address Register */
#define CQSPI_REG_REMAP                  0x24u
#define   CQSPI_REMAP_ADDR_LSB           0u          /* Remapping of incoming AHB address */
#define   CQSPI_REMAP_ADDR_MASK          0xFFFFFFFFu

/* Mode Bit Configuration Register */
#define CQSPI_REG_MODE_BIT               0x28u
#define   CQSPI_MODE_BITS_LSB            0u          /* Mode bits */
#define   CQSPI_MODE_BITS_MASK           0xFFu
#define   CQSPI_MODE_CRC_CHUNK_LSB       8u          /* CRC chunk size */
#define   CQSPI_MODE_CRC_CHUNK_MASK      0x7u
#define   CQSPI_MODE_CRC_EN              (1u << 15)  /* CRC# output enable bit */
#define   CQSPI_MODE_RX_CRC_UPPER_LSB    16u         /* RX CRC data (upper) */
#define   CQSPI_MODE_RX_CRC_UPPER_MASK   0xFFu
#define   CQSPI_MODE_RX_CRC_LOWER_LSB    24u         /* RX CRC data (lower) */
#define   CQSPI_MODE_RX_CRC_LOWER_MASK   0xFFu

/* SRAM Fill Level Register */
#define CQSPI_REG_SDRAMLEVEL             0x2Cu
#define   CQSPI_SDRAMLEVEL_RD_LSB        0u          /* SRAM Fill Level (Indirect Read Partition) */
#define   CQSPI_SDRAMLEVEL_RD_MASK       0xFFFFu
#define   CQSPI_SDRAMLEVEL_WR_LSB        16u         /* SRAM Fill Level (Indirect Write Partition) */
#define   CQSPI_SDRAMLEVEL_WR_MASK       0xFFFFu

/* TX Threshold Register */
#define CQSPI_REG_TXTHRESH               0x30u
#define   CQSPI_TX_THRESH_LSB            0u          /* TX FIFO threshold level */
#define   CQSPI_TX_THRESH_MASK           0x1Fu

/* RX Threshold Register */
#define CQSPI_REG_RXTHRESH               0x34u
#define   CQSPI_RX_THRESH_LSB            0u          /* RX FIFO threshold level */
#define   CQSPI_RX_THRESH_MASK           0x1Fu

/* Write Completion Control Register */
#define CQSPI_REG_WR_COMPLETION          0x38u
#define   CQSPI_WR_POLL_OPCODE_LSB       0u          /* Polling opcode */
#define   CQSPI_WR_POLL_OPCODE_MASK      0xFFu
#define   CQSPI_WR_POLL_BIT_INDEX_LSB    8u          /* Polling bit index */
#define   CQSPI_WR_POLL_BIT_INDEX_MASK   0x7u
#define   CQSPI_WR_POLL_ADDR_EN          (1u << 11)  /* Polling address phase enable */
#define   CQSPI_WR_POLL_POLARITY         (1u << 13)  /* Polling polarity */
#define   CQSPI_WR_POLL_DISABLE          (1u << 14)  /* Disable polling */
#define   CQSPI_WR_POLL_EXP_EN           (1u << 15)  /* Enable polling expiration */
#define   CQSPI_WR_POLL_COUNT_LSB        16u         /* Polling count */
#define   CQSPI_WR_POLL_COUNT_MASK       0xFFu
#define   CQSPI_WR_POLL_DELAY_LSB        24u         /* Polling repetition delay */
#define   CQSPI_WR_POLL_DELAY_MASK       0xFFu

/* Polling Expiration Register */
#define CQSPI_REG_POLL_EXP               0x3Cu
#define   CQSPI_POLL_EXP_LSB             0u          /* Polling expiration cycles */
#define   CQSPI_POLL_EXP_MASK            0xFFFFFFFFu

/* Interrupt Status Register */
#define CQSPI_REG_IRQSTATUS              0x40u
#define   CQSPI_IRQ_MODE_FAIL            (1u << 0)   /* Mode fail */
#define   CQSPI_IRQ_UNDERFLOW            (1u << 1)   /* Underflow Detected */
#define   CQSPI_IRQ_INDIRECT_COMPLETE    (1u << 2)   /* Controller has completed last triggered indirect operation */
#define   CQSPI_IRQ_INDIRECT_REJECT      (1u << 3)   /* Indirect operation was requested but could not be accepted */
#define   CQSPI_IRQ_PROT_AREA            (1u << 4)   /* Write to protected area was attempted and rejected */
#define   CQSPI_IRQ_ILLEGAL_AHB          (1u << 5)   /* Illegal AHB Access Detected */
#define   CQSPI_IRQ_WATERMARK            (1u << 6)   /* Indirect Transfer Watermark Level Breached */
#define   CQSPI_IRQ_RX_OVERFLOW          (1u << 7)   /* Receive Overflow */
#define   CQSPI_IRQ_TX_NOT_FULL          (1u << 8)   /* Small TX FIFO not full */
#define   CQSPI_IRQ_TX_FULL              (1u << 9)   /* Small TX FIFO full */
#define   CQSPI_IRQ_RX_NOT_EMPTY         (1u << 10)  /* Small RX FIFO not empty */
#define   CQSPI_IRQ_RX_FULL              (1u << 11)  /* Small RX FIFO full */
#define   CQSPI_IRQ_SRAM_FULL            (1u << 12)  /* Indirect Read partition of SRAM is full */
#define   CQSPI_IRQ_POLL_EXPIRE          (1u << 13)  /* The maximum number of programmed polls cycles is expired */
#define   CQSPI_IRQ_STIG_COMPLETE        (1u << 14)  /* The STIG request completion interrupt */
#define   CQSPI_IRQ_RX_CRC_ERROR         (1u << 16)  /* RX CRC data error */
#define   CQSPI_IRQ_RX_CRC_VALID         (1u << 17)  /* RX CRC data valid */
#define   CQSPI_IRQ_TX_CRC_BROKEN        (1u << 18)  /* TX CRC chunk was broken */
#define   CQSPI_IRQ_ECC_FAIL             (1u << 19)  /* ECC failure */

/* Interrupt Mask Register */
#define CQSPI_REG_IRQMASK                0x44u
#define   CQSPI_IRQ_MASK_MODE_FAIL       (1u << 0)   /* Mode fail interrupt mask */
#define   CQSPI_IRQ_MASK_UNDERFLOW       (1u << 1)   /* Underflow interrupt mask */
#define   CQSPI_IRQ_MASK_INDIRECT_COMPLETE (1u << 2) /* Indirect complete interrupt mask */
#define   CQSPI_IRQ_MASK_INDIRECT_REJECT (1u << 3)   /* Indirect reject interrupt mask */
#define   CQSPI_IRQ_MASK_PROT_AREA       (1u << 4)   /* Protected area interrupt mask */
#define   CQSPI_IRQ_MASK_ILLEGAL_AHB     (1u << 5)   /* Illegal AHB interrupt mask */
#define   CQSPI_IRQ_MASK_WATERMARK       (1u << 6)   /* Watermark interrupt mask */
#define   CQSPI_IRQ_MASK_RX_OVERFLOW     (1u << 7)  /* RX overflow interrupt mask */
#define   CQSPI_IRQ_MASK_TX_NOT_FULL     (1u << 8)  /* TX not full interrupt mask */
#define   CQSPI_IRQ_MASK_TX_FULL         (1u << 9)  /* TX full interrupt mask */
#define   CQSPI_IRQ_MASK_RX_NOT_EMPTY    (1u << 10)  /* RX not empty interrupt mask */
#define   CQSPI_IRQ_MASK_RX_FULL         (1u << 11)  /* RX full interrupt mask */
#define   CQSPI_IRQ_MASK_SRAM_FULL       (1u << 12)  /* SRAM full interrupt mask */
#define   CQSPI_IRQ_MASK_POLL_EXPIRE     (1u << 13)  /* Poll expire interrupt mask */
#define   CQSPI_IRQ_MASK_STIG_COMPLETE   (1u << 14)  /* STIG complete interrupt mask */
#define   CQSPI_IRQ_MASK_RX_CRC_ERROR    (1u << 16)  /* RX CRC error interrupt mask */
#define   CQSPI_IRQ_MASK_RX_CRC_VALID    (1u << 17)  /* RX CRC valid interrupt mask */
#define   CQSPI_IRQ_MASK_TX_CRC_BROKEN   (1u << 18)  /* TX CRC broken interrupt mask */
#define   CQSPI_IRQ_MASK_ECC_FAIL        (1u << 19)  /* ECC fail interrupt mask */

/* Lower Write Protection Register */
#define CQSPI_REG_WR_PROT_LOWER          0x50u
#define   CQSPI_WR_PROT_LOWER_LSB        0u          /* Lower write protection block number */
#define   CQSPI_WR_PROT_LOWER_MASK       0xFFFFFFFFu

/* Upper Write Protection Register */
#define CQSPI_REG_WR_PROT_UPPER          0x54u
#define   CQSPI_WR_PROT_UPPER_LSB        0u          /* Upper write protection block number */
#define   CQSPI_WR_PROT_UPPER_MASK       0xFFFFFFFFu

/* Write Protection Register */
#define CQSPI_REG_WR_PROT                0x58u
#define   CQSPI_WR_PROT_INVERT           (1u << 0)   /* Write Protection Inversion Bit */
#define   CQSPI_WR_PROT_EN               (1u << 1)   /* Write Protection Enable Bit */

/* Indirect Read Transfer Control Register */
#define CQSPI_REG_INDIRECTRD             0x60u
#define   CQSPI_INDIRECTRD_START         (1u << 0)   /* Start indirect read (control) */
#define   CQSPI_INDIRECTRD_CANCEL        (1u << 1)   /* Cancel indirect read (control) */
#define   CQSPI_INDIRECTRD_IN_PROGRESS   (1u << 2)   /* Indirect read operation in progress (status) */
#define   CQSPI_INDIRECTRD_SRAM_FULL     (1u << 3)   /* SRAM full (status) */
#define   CQSPI_INDIRECTRD_QUEUED        (1u << 4)   /* Two indirect read operations have been queued (status) */
#define   CQSPI_INDIRECTRD_DONE          (1u << 5)   /* Indirect Completion Status (status) */
#define   CQSPI_INDIRECTRD_COMPLETED_LSB 6u          /* Number of indirect operations completed */
#define   CQSPI_INDIRECTRD_COMPLETED_MASK 0x3u

/* Indirect Read Transfer Watermark Register */
#define CQSPI_REG_INDIRECTRDWATERMARK    0x64u
#define   CQSPI_INDIRECTRD_WATERMARK_LSB 0u          /* Watermark value */
#define   CQSPI_INDIRECTRD_WATERMARK_MASK 0xFFFFFFFFu

/* Indirect Read Transfer Start Address Register */
#define CQSPI_REG_INDIRECTRDSTARTADDR    0x68u
#define   CQSPI_INDIRECTRD_START_ADDR_LSB 0u          /* Start of Indirect Access */
#define   CQSPI_INDIRECTRD_START_ADDR_MASK 0xFFFFFFFFu

/* Indirect Read Transfer Number Bytes Register */
#define CQSPI_REG_INDIRECTRDBYTES        0x6Cu
#define   CQSPI_INDIRECTRD_NUM_BYTES_LSB 0u          /* Indirect Number of Bytes */
#define   CQSPI_INDIRECTRD_NUM_BYTES_MASK 0xFFFFFFFFu

/* Indirect Write Transfer Control Register */
#define CQSPI_REG_INDIRECTWR             0x70u
#define   CQSPI_INDIRECTWR_START         (1u << 0)   /* Start indirect write (control) */
#define   CQSPI_INDIRECTWR_CANCEL        (1u << 1)   /* Cancel indirect write (control) */
#define   CQSPI_INDIRECTWR_IN_PROGRESS   (1u << 2)   /* Indirect write operation in progress (status) */
#define   CQSPI_INDIRECTWR_QUEUED        (1u << 4)   /* Two indirect write operations have been queued (status) */
#define   CQSPI_INDIRECTWR_DONE          (1u << 5)   /* Indirect Completion Status (status) */
#define   CQSPI_INDIRECTWR_COMPLETED_LSB 6u          /* Number of indirect operations completed */
#define   CQSPI_INDIRECTWR_COMPLETED_MASK 0x3u

/* Indirect Write Transfer Watermark Register */
#define CQSPI_REG_INDIRECTWRWATERMARK    0x74u
#define   CQSPI_INDIRECTWR_WATERMARK_LSB 0u          /* Watermark value */
#define   CQSPI_INDIRECTWR_WATERMARK_MASK 0xFFFFFFFFu

/* Indirect Write Transfer Start Address Register */
#define CQSPI_REG_INDIRECTWRSTARTADDR    0x78u
#define   CQSPI_INDIRECTWR_START_ADDR_LSB 0u          /* Start of Indirect Access */
#define   CQSPI_INDIRECTWR_START_ADDR_MASK 0xFFFFFFFFu

/* Indirect Write Transfer Number Bytes Register */
#define CQSPI_REG_INDIRECTWRBYTES        0x7Cu
#define   CQSPI_INDIRECTWR_NUM_BYTES_LSB 0u          /* Indirect Number of Bytes */
#define   CQSPI_INDIRECTWR_NUM_BYTES_MASK 0xFFFFFFFFu

/* Indirect Trigger Address Range Register */
#define CQSPI_REG_INDIRECT_RANGE         0x80u
#define   CQSPI_INDIRECT_RANGE_WIDTH_LSB 0u          /* Indirect Range Width */
#define   CQSPI_INDIRECT_RANGE_WIDTH_MASK 0xFu

/* Flash Command Control Memory Register (Using STIG) */
#define CQSPI_REG_FLASH_CMD_CTRL_MEM     0x8Cu
#define   CQSPI_FLASH_CMD_MEM_TRIGGER    (1u << 0)   /* Trigger the Memory Bank data request */
#define   CQSPI_FLASH_CMD_MEM_IN_PROGRESS (1u << 1)  /* Memory Bank data request in progress */
#define   CQSPI_FLASH_CMD_MEM_DATA_LSB   8u          /* Memory Bank Read Data */
#define   CQSPI_FLASH_CMD_MEM_DATA_MASK  0xFFu
#define   CQSPI_FLASH_CMD_MEM_NUM_BYTES_LSB 16u      /* Number of STIG Memory Bank Read Bytes */
#define   CQSPI_FLASH_CMD_MEM_NUM_BYTES_MASK 0x7u
#define   CQSPI_FLASH_CMD_MEM_ADDR_LSB   20u         /* Memory Bank Address */
#define   CQSPI_FLASH_CMD_MEM_ADDR_MASK  0x1FFu

/* STIG Memory Bank depth (compile-time integration param). Default pow2=4 => 16 bytes. */
#ifndef CQSPI_STIG_MEM_BANK_DEPTH_POW2
#define CQSPI_STIG_MEM_BANK_DEPTH_POW2   4u
#endif
#define CQSPI_STIG_MEM_BANK_MAX_BYTES    (1u << CQSPI_STIG_MEM_BANK_DEPTH_POW2)

/* Flash Command Control Register (Using STIG) */
#define CQSPI_REG_CMDCTRL                0x90u
#define   CQSPI_CMDCTRL_EXECUTE          (1u << 0)   /* Execute the command */
#define   CQSPI_CMDCTRL_INPROGRESS       (1u << 1)   /* STIG command execution in progress */
#define   CQSPI_CMDCTRL_MEM_BANK_EN      (1u << 2)   /* STIG Memory Bank enable bit */
#define   CQSPI_CMDCTRL_DUMMY_LSB        7u          /* Number of Dummy Cycles */
#define   CQSPI_CMDCTRL_DUMMY_MASK       0x1Fu
#define   CQSPI_CMDCTRL_WR_BYTES_LSB     12u         /* Number of Write Data Bytes */
#define   CQSPI_CMDCTRL_WR_BYTES_MASK    0x7u
#define   CQSPI_CMDCTRL_WR_EN_LSB        15u         /* Write Data Enable */
#define   CQSPI_CMDCTRL_ADD_BYTES_LSB    16u         /* Number of Address Bytes */
#define   CQSPI_CMDCTRL_ADD_BYTES_MASK   0x3u
#define   CQSPI_CMDCTRL_MODE_EN          (1u << 18)  /* Mode Bit Enable */
#define   CQSPI_CMDCTRL_ADDR_EN_LSB      19u         /* Command Address Enable */
#define   CQSPI_CMDCTRL_RD_BYTES_LSB     20u         /* Number of Read Data Bytes */
#define   CQSPI_CMDCTRL_RD_BYTES_MASK    0x7u
#define   CQSPI_CMDCTRL_RD_EN_LSB        23u         /* Read Data Enable */
#define   CQSPI_CMDCTRL_OPCODE_LSB       24u         /* Command Opcode */
#define   CQSPI_CMDCTRL_OPCODE_MASK      0xFFu

/* Flash Command Address Register */
#define CQSPI_REG_CMDADDRESS             0x94u
#define   CQSPI_CMD_ADDR_LSB             0u          /* Command Address */
#define   CQSPI_CMD_ADDR_MASK            0xFFFFFFFFu

/* Flash Command Read Data Register (Lower) */
#define CQSPI_REG_CMDREADDATALOWER       0xA0u       /* Flash Command Read Data Register (Lower) - Command Read Data (Lower) */

/* Flash Command Read Data Register (Upper) */
#define CQSPI_REG_CMDREADDATAUPPER       0xA4u       /* Flash Command Read Data Register (Upper) - Command Read Data (Upper) */

/* Flash Command Write Data Register (Lower) */
#define CQSPI_REG_CMDWRITEDATALOWER      0xA8u       /* Flash Command Write Data Register (Lower) - Command Write Data (Lower) */

/* Flash Command Write Data Register (Upper) */
#define CQSPI_REG_CMDWRITEDATAUPPER      0xACu       /* Flash Command Write Data Register (Upper) - Command Write Data (Upper) */

/* Polling Flash Status Register */
#define CQSPI_REG_POLLING_FLASH_STATUS   0xB0u
#define   CQSPI_POLLING_DUMMY_CYCLES_LSB 16u         /* Number of dummy cycles for auto-polling */
#define   CQSPI_POLLING_DUMMY_CYCLES_MASK 0x1Fu
#define   CQSPI_POLLING_STATUS_VALID     (1u << 8)   /* Polling Status Valid */
#define   CQSPI_POLLING_FLASH_STATUS_LSB 0u          /* Flash Status */
#define   CQSPI_POLLING_FLASH_STATUS_MASK 0xFFu

/* Opcode Extension Register (Lower) */
#define CQSPI_REG_OPCODE_EXT_LOWER       0xE0u
#define   CQSPI_OPCODE_EXT_RD_LSB        24u         /* Supplement byte of Read Opcode */
#define   CQSPI_OPCODE_EXT_RD_MASK       0xFFu
#define   CQSPI_OPCODE_EXT_WR_LSB        16u         /* Supplement byte of Write Opcode */
#define   CQSPI_OPCODE_EXT_WR_MASK       0xFFu
#define   CQSPI_OPCODE_EXT_POLL_LSB      8u          /* Supplement byte of Polling Opcode */
#define   CQSPI_OPCODE_EXT_POLL_MASK     0xFFu
#define   CQSPI_OPCODE_EXT_STIG_LSB      0u          /* Supplement byte of STIG Opcode */
#define   CQSPI_OPCODE_EXT_STIG_MASK     0xFFu

/* Opcode Extension Register (Upper) */
#define CQSPI_REG_OPCODE_EXT_UPPER       0xE4u
#define   CQSPI_OPCODE_EXT_WEL1_LSB      24u         /* WEL Opcode byte 1 */
#define   CQSPI_OPCODE_EXT_WEL1_MASK     0xFFu
#define   CQSPI_OPCODE_EXT_WEL2_LSB      16u         /* WEL Opcode byte 2 (Optional) */
#define   CQSPI_OPCODE_EXT_WEL2_MASK     0xFFu

/* Module ID Register */
#define CQSPI_REG_MODULE_ID              0xFCu
#define   CQSPI_MODULE_ID_FIX_LSB        24u         /* Fix/patch number */
#define   CQSPI_MODULE_ID_FIX_MASK       0xFFu
#define   CQSPI_MODULE_ID_REV_LSB        8u          /* Module/Revision ID */
#define   CQSPI_MODULE_ID_REV_MASK       0xFFFFu
#define   CQSPI_MODULE_ID_CONFIG_LSB     0u          /* Configuration ID number */
#define   CQSPI_MODULE_ID_CONFIG_MASK    0x3u

/* Transfer width encodings */
#define CQSPI_INST_TYPE_SINGLE           0u
#define CQSPI_INST_TYPE_DUAL             1u
#define CQSPI_INST_TYPE_QUAD             2u

/* Common SPI flash opcodes (W25Qxx) */
#define W25Q_CMD_RDID    0x9Fu
#define W25Q_CMD_DEVID   0xABu  /* Device ID */
#define W25Q_CMD_MANDEV  0x90u  /* Manufacturer/Device ID */
#define W25Q_CMD_UNIQUE  0x4Bu  /* Read Unique ID */
#define W25Q_CMD_RDSR1   0x05u
#define W25Q_CMD_RDSR2   0x35u
#define W25Q_CMD_RDSR3   0x15u
#define W25Q_CMD_WREN    0x06u
#define W25Q_CMD_PP      0x02u
#define W25Q_CMD_PP_QUAD 0x32u  /* Quad Input Page Program */
#define W25Q_CMD_READ    0x03u
#define W25Q_CMD_FAST    0x0Bu
#define W25Q_CMD_QUAD_READ 0x6Bu  /* Quad Output Fast Read */
#define W25Q_CMD_QUAD_FAST 0xEBu  /* Quad I/O Fast Read */
#define W25Q_CMD_MANDEV_DUAL 0x92u  /* Manufacturer/Device ID Dual I/O */
#define W25Q_CMD_MANDEV_QUAD 0x94u  /* Manufacturer/Device ID Quad I/O */
#define W25Q_CMD_SE_4K   0x20u
#define W25Q_CMD_BE_32K  0x52u  /* Block Erase (32KB) */
#define W25Q_CMD_BE_64K  0xD8u
#define W25Q_CMD_CE      0xC7u
#define W25Q_CMD_SFDP    0x5Au  /* Read SFDP Register */
/* Addressing mode */
#define W25Q_CMD_EN4B    0xB7u
#define W25Q_CMD_EX4B    0xE9u
/* Reset commands */
#define W25Q_CMD_RSTEN   0x66u  /* Enable Reset */
#define W25Q_CMD_RST     0x99u  /* Reset Device */
/* Status register write ops (Winbond) */
#define W25Q_CMD_WRSR12  0x01u  /* write SR1 then SR2 */
#define W25Q_CMD_WRSR2   0x31u  /* write SR2 only */
#define W25Q_CMD_WRSR3   0x11u  /* write SR3 only */

int qspi_unlock_all(void);
/* 控制驱动内部调试打印（默认开启）。带宽测试时建议关闭以避免串口开销影响计时。 */
void qspi_set_verbose(bool enable);

typedef struct
{
    volatile uint8_t *reg;   /* APB config base: QSPI_CFG_BASE */
    volatile uint8_t *ahb;   /* AHB mapped flash window: M4_SLV_FLASH_BASE */
    uint32_t ahb_size;       /* Window size in bytes */
    uint32_t ref_clk_hz;     /* Reference clock to controller (AHB clock) */
    uint32_t sclk_hz;        /* Desired SPI SCLK */
    uint32_t fifo_depth;     /* words */
    uint32_t fifo_width;     /* bytes per word (4) */
    uint32_t page_size;      /* 256 */
    uint32_t block_4k_units; /* 16 => 64KB blocks (for SIZE reg) */
} qspi_cadence_t;

/* Global singleton for simplicity */
extern qspi_cadence_t g_qspi;

void qspi_cadence_init(uint32_t ref_clk_hz, uint32_t sclk_hz);
/* 读取当前控制器配置得到的实际 SCLK（根据 BAUD 分频字段计算） */
uint32_t qspi_get_actual_sclk_hz(void);
/* 返回当前 BAUD 寄存器 raw 值（0..15），含义为 SCLK = ref/(2*(raw+1)) */
uint32_t qspi_get_baud_raw(void);

int qspi_read_id(uint8_t *id, uint32_t len);
int qspi_read_device_id(uint8_t *dev_id);
int qspi_read_manufacturer_device_id(uint8_t *mfg_id, uint8_t *dev_id);
int qspi_read_unique_id(uint8_t *uid, uint32_t len);
int qspi_read_sfdp(uint32_t addr, uint8_t *buf, uint32_t len);
int qspi_read(uint32_t addr, void *buf, uint32_t len);
int qspi_page_program(uint32_t addr, const void *buf, uint32_t len);
int qspi_erase_4k(uint32_t addr);
int qspi_erase_32k(uint32_t addr);
int qspi_erase_64k(uint32_t addr);
int qspi_chip_erase(void);
int qspi_reset_enable(void);
int qspi_reset_device(void);
int qspi_software_reset(void);
int qspi_wait_ready(uint32_t timeout_ms);
/* Feature helpers */
int qspi_set_quad_enable(bool enable);
int qspi_set_address_mode_4byte(bool enable);
/* XIP 1-4-4 配置：将控制器设置为以 0xEB 指令 1-4-4 连续读进入 XIP，
 * addr_bytes 取 3 或 4；dummy_cycles 常用 6~8；mode_bits 常用 0x00。*/
int qspi_enter_xip_144(unsigned addr_bytes, unsigned dummy_cycles, uint8_t mode_bits);
void qspi_exit_xip_mode(void);

/* Debug helpers */
void qspi_dump_regs(const char *tag);
int qspi_read_status(uint8_t *sr1, uint8_t *sr2, uint8_t *sr3);

/* SRAM partition helpers */
/* Set indirect read partition size in units of locations (each 4 bytes).
 * Valid range: 2 .. (CQSPI_SRAM_TOTAL_LOCATIONS - 1). Values will be clamped.
 * Returns 0 on success, <0 on error (e.g., controller not idle timeout). */
int qspi_set_sram_partition(uint32_t read_locations);
/* Get current partition sizes (locations). Any pointer can be NULL. */
void qspi_get_sram_partition(uint32_t *read_locations, uint32_t *write_locations);

/* Generic STIG helpers (support <=8B direct or Memory Bank for 16..512B) */
int qspi_stig_read_ex(uint8_t opcode, uint32_t addr, unsigned addr_bytes, unsigned dummy_cycles, void *rx, uint32_t rx_len);
int qspi_stig_write_ex(uint8_t opcode, uint32_t addr, unsigned addr_bytes, unsigned dummy_cycles, const void *tx, uint32_t tx_len);

#ifdef __cplusplus
}
#endif

#endif /* S300_BSP_QSPI_CADENCE_H */
