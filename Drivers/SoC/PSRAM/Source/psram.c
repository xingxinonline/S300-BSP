#include "psram.h"
extern unsigned int psram_reg[];
int init_psram(uint32_t ahbclock,uint32_t psramclock)
{
	uint32_t temp;
	
	PSRAM_XIP_DIS;
	PSRAM_SSI_SSIENR = 0;
	PSRAM_SSI_SER = 0;
	PSRAM_SSI_IMR = 0;
	// PSRAM_SSI_DMACR = 0;
	// PSRAM_SSI_DMATDLR = 0;
	// PSRAM_SSI_DMARDLR = 0;
	PSRAM_SSI_CTRLR1 = 0;
	PSRAM_SSI_TXFTLR = 0;
	PSRAM_SSI_RXFTLR = 0;
	// PSRAM_SSI_AXIAWLEN = 0;
    set_psram_state();

	// Instruction phase
	// Address phase
	// Wait cycles
	// Data phase
	// ■ CTRLR0.SPI_FRF - Specifies the format in which the transmission happens for the frame.
	// ■ CTRLR0.DFS – Specifies data length.
	// temp = PSRAM_SSI_CTRLR0;
	// temp &= 3 << 10;
	temp = 0;
	// temp &= ~(1UL << 31 | 1 << 25 | 1 << 24 | 3 << 22 | 0xF << 16 | 1 << 14 | 1 << 13 | 3 << 10 | 3 << 8| 3 << 6 | 0x1F);
	temp |= 0
		 |  1 << 31 //This field selects if DWC_ssi is working in Master or Slave mode
		 |  1 << 25 //Enable Dynamic wait states in SPI mode of operation
		 |  1 << 24 //SPI Hyperbus Frame format enable.
		 |  3 << 22//SPI_FRF SPI Frame Format 0x0 (SPI_STANDARD):0x1 (SPI_DUAL):0x2 (SPI_QUAD):0x3 (SPI_OCTAL):
		 |  0 << 14//SSTE Slave Select Toggle Enable.0x1 (TOGGLE_EN):0x0 (TOGGLE_DISABLE):
		 |  2 << 10//TMOD Transfer Mode.0x0 (TX_AND_RX):0x1 (TX_ONLY):0x2 (RX_ONLY):0x3 (EEPROM_READ):
		 |  0 << 9//SCPOL
		 |  0 << 8//SCPH
		 |  0 << 6//FRF Frame Format.0x0 (SPI):0x1 (SSP):0x2 (MICROWIRE): National Semiconductors Microwire Frame Format
		 |  0x7//DFS n-1 0x3 (DFS_04_BIT): 04-bit serial data transfer -> 0x1f (DFS_32_BIT): 32-bit serial data transfer
		 ;
	PSRAM_SSI_CTRLR0 = temp;
	temp = PSRAM_SSI_CTRLR0;
	
	// ■ SPI_CTRLR0 (SPI Control Register 0 register) – Specifies length of instruction, address, and data.
	// ■ SPI_CTRLR0.INST_L – Specifies length of an instruction (possible values for instruction length are
	// 0, 4, 8, or 16 bits.)
	// ■ SPI_CTRLR0.ADDR_L – Specifies address length
	// temp = PSRAM_SSI_SPI_CTRLR0;
	// temp &= 0xf << 2;
	temp = 0;
	// temp &= ~(1 << 30 | 1 << 29 | 3 << 26  | 3 << 24 | 3 << 20 | 0xF << 16 | 0x1F << 11  | 0x3 << 8 | 0xF << 2 | 0x3);
	temp |= 0
		 |  1 << 30 //Enables clock stretching capability in SPI transfers.
		 |  0 << 29//XIP_PREFETCH_EN Enables XIP pre-fetch functionality in DWC_ssi.
		 |  0 << 26//XIP_MBL XIP Mode bits length
		 |  1 << 25//SPI_RXDS_SIG_EN  Enable rxds signaling during address and command phase of Hypebus transfer.
		 |  1 << 24//SPI_DM_EN SPI data mask enable bit.
		 |  0 << 21//SSIC_XIP_CONT_XFER_EN Enable continuous transfer in XIP mode.
		 |  1 << 20//XIP_INST_EN XIP instruction enable bit.
		 |  0 << 19//XIP_DFS_HC Fix DFS for XIP transfers
		 |  1 << 18//SPI_RXDS_EN Read data strobe enable bit.
		 |  1 << 17//INST_DDR_EN Instruction DDR Enable bit.
		 |  1 << 16//SPI_DDR_EN SPI DDR Enable bit.
		 |  7 << 11//WAIT_CYCLES 0-1f
		 |  0 << 8//INST_L Dual/Quad/Octal mode instruction length in bits.
					//0x0 (INST_L0): No Instruction
					//0x1 (INST_L4): 4 bit Instruction length
					//0x2 (INST_L8): 8 bit Instruction length
					//0x3 (INST_L16): 16 bit Instruction length
		 |  0 << 7//XIP_MD_BIT_EN Mode bits enable in XIP mode
		 |  0xc << 2//ADDR_L This bit defines Length of Address to be transmitted
					//0x0 (ADDR_L0): No Address
					//0x1 (ADDR_L4): 4 bit Address length
					//0x2 (ADDR_L8): 8 bit Address length
					//0x3 (ADDR_L12): 12 bit Address length
					//0x4 (ADDR_L16): 16 bit Address length
					//0x5 (ADDR_L20): 20 bit Address length
					//0x6 (ADDR_L24): 24 bit Address length
					//0x7 (ADDR_L28): 28 bit Address length
					//0x8 (ADDR_L32): 32 bit Address length
		 |  2//TRANS_TYPE Address and instruction transfer format.
		 		//0x0 (TT0): Instruction and Address will be sent in Standard SPI Mode.
				//0x1 (TT1): Instruction will be sent in Standard SPI Mode and Address will be sent in the mode specified by CTRLR0.SPI_FRF.
				//0x2 (TT2): Both Instruction and Address will be sent in the mode specified by SPI_FRF.
		 ;
	PSRAM_SSI_SPI_CTRLR0 = temp;
	temp = PSRAM_SSI_SPI_CTRLR0;

    temp = ahbclock/(psramclock);
	PSRAM_SSI_BAUDR = temp;//
	PSRAM_SSI_RX_SAMPLE_DELAY = 1 << 16 | 10;
	PSRAM_SSI_DDR_DRIVE_EDGE = temp/2 - 1;
	PSRAM_SSI_DDR_DRIVE_EDGE = 1;
	PSRAM_SSI_TXFTLR = 0;
	PSRAM_SSI_RXFTLR = 0;
	PSRAM_SSI_IMR = 0;

	//SSIC_CONCURRENT_XIP_EN == 1
	// if(PSRAM_SSI_SPI_CTRLR0 & 0x200000)
	{
	// temp = PSRAM_SSI_XIP_CTRL;
	temp = 0;
			//                                         0x1f
	temp &= ~(1 << 29 | 3 << 26  | 7 << 22 | 7 << 18 | 0x0 << 13 | 0x3 << 9  | 0xf << 4 | 0x3 << 2 | 0x3);
	temp |= 0
		 |  0 << 29 //Enables XIP pre-fetch functionality
		 |  2 << 26 //XIP Mode bits length
		 |  1 << 25 //Enable rxds signaling during address and command phase of Hyperbus transfer.
		 |  1 << 24 //SPI Hyperbus Frame format enable for XIP transfers.
		 |  0 << 23 //Enable continuous transfer in XIP mode.
		 |  1 << 22 //XIP instruction enable bit.
		 |  1 << 21 //Read data strobe enable bit.
		 |  1 << 20 //Instruction DDR Enable bit
		 |  1 << 19 //SPI DDR Enable bit
		 |  0 << 18 //Fix DFS for XIP transfers.
		 |  7 << 13 //Wait cycles in Dual/Quad/Octal mode between control frames
		 |  0 << 12 //Mode bits enable in XIP mode.
		 |  0 << 9 //Dual/Quad/Octal mode instruction length in bits.0 null; 1 4bit; 2 8bit;3 16bit
		 |  0xc << 4 //This bit defines Length of Address to be transmitted
		 |  2 << 2 //TRANS_TYPE Address and instruction transfer format.
		 |  3; //FRF SPI Frame Format Octal SPI Format
	
	PSRAM_SSI_XIP_CTRL = temp;
	temp = PSRAM_SSI_XIP_CTRL;

	//SSIC_CONCURRENT_XIP_EN == 1
	temp = PSRAM_SSI_XRXOICR;
	}
	//SSIC_SPI_DYN_WS_EN == 1
	if(PSRAM_SSI_CTRLR0 & 0x2000000)
	{
	temp = PSRAM_SSI_SPI_CTRLR1;
	temp &= ~(0xf << 8 | 0x7);
	temp |= 0
		 |  0 << 8//Maximum wait cycles allowed per transaction.
		 |  0; //SPI Dynamic Wait states field.
	PSRAM_SSI_SPI_CTRLR1 = temp;
	temp = PSRAM_SSI_SPITECR;
	}


	PSRAM_SSI_XIP_WRITE_INCR_INST = 0x0002;
	PSRAM_SSI_XIP_WRITE_WRAP_INST = 0x0000;
	PSRAM_SSI_XIP_INCR_INST = 0x000a;
	PSRAM_SSI_XIP_WRAP_INST = 0x0008;
	// temp = PSRAM_SSI_XIP_WRITE_CTRL;
	temp = 0;
	temp &= ~(0x1f << 16 | 7 << 10 | 3 << 8 | 0xff);
	temp |= 0
		 |  7 << 16//Wait cycles in Dual/Quad/Octal mode between control frames
		 |  1 << 13//Enable rxds signaling during address and command phase of Hyperbus transfer.
		 |  1 << 12//SPI Hyperbus Frame format enable for XIP Write transfers.
		 |  1 << 11//Instruction DDR Enable bit.
		 |  1 << 10//SPI DDR Enable bit.
		 |  0 << 8//Dual/Quad/Octal mode instruction length in bits.0 null; 1 4bit; 2 8bit;3 16bit
		 |  0xc << 4//This bit defines Length of Address to be transmitted
		 |  2 << 2//Address and instruction transfer format.
		 |  3;//SPI Frame Format Octal SPI Format
	PSRAM_SSI_XIP_WRITE_CTRL = temp;
	PSRAM_SSI_XIP_MODE_BITS = 0x00;//Micron No support for Basic-XIP:0x00 Supporting Basic-XIP:0x80 ;Winbond 0x20;Spansion 0xa0;


	PSRAM_SSI_XIP_SER |= 1;
	PSRAM_SSI_SER |= 1;
	PSRAM_SSI_SSIENR |= 1;
	while ((PSRAM_SSI_SR & PSRAM_BUSY));
	PSRAM_XIP_EN;
    return 0;
}

void set_psram_state(void)
{
    uint32_t temp;
	temp = PSRAM_SSI_TXEICR;
	temp = PSRAM_SSI_RXOICR;
	temp = PSRAM_SSI_RXUICR;
	temp = PSRAM_SSI_MSTICR;
	temp = PSRAM_SSI_ICR;
	temp = PSRAM_SSI_SR;

	// temp = PSRAM_SSI_XRXOICR;
	// temp = PSRAM_SSI_SPITECR;

    (void)temp;
}

