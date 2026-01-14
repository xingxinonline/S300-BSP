#ifndef S300_DMA_S300_H
#define S300_DMA_S300_H

#ifdef __cplusplus
extern "C" {
#endif

#include "s300.h"

/*
 * Note: DMA channel registers are 64-bit spaced in the memory map on S300.
 * Many registers are 32-bit wide with the upper 32 bits reserved. We model
 * those by inserting a 32-bit reserved field to preserve correct offsets.
 * For 64-bit logical registers split into low/high halves (e.g., CTL, CFG),
 * we expose both 32-bit halves explicitly as *_L and *_H.
 */
typedef struct
{
    volatile uint32_t SAR;     /* 0x00 */
    volatile uint32_t _RSV0;   /* 0x04 */
    volatile uint32_t DAR;     /* 0x08 */
    volatile uint32_t _RSV1;   /* 0x0C */
    volatile uint32_t LLP;     /* 0x10 */
    volatile uint32_t _RSV2;   /* 0x14 */
    volatile uint32_t CTL_L;   /* 0x18 */
    volatile uint32_t CTL_H;   /* 0x1C */
    volatile uint32_t SSTAT;   /* 0x20 */
    volatile uint32_t _RSV3;   /* 0x24 */
    volatile uint32_t DSTAT;   /* 0x28 */
    volatile uint32_t _RSV4;   /* 0x2C */
    volatile uint32_t SSTATAR; /* 0x30 */
    volatile uint32_t _RSV5;   /* 0x34 */
    volatile uint32_t DSTATAR; /* 0x38 */
    volatile uint32_t _RSV6;   /* 0x3C */
    volatile uint32_t CFG_L;   /* 0x40 */
    volatile uint32_t CFG_H;   /* 0x44 */
    volatile uint32_t SGR;     /* 0x48 */
    volatile uint32_t _RSV7;   /* 0x4C */
    volatile uint32_t DSR;     /* 0x50 */
    volatile uint32_t _RSV8;   /* 0x54 */
} S300_DMA_Channel_TypeDef;

typedef struct
{
    S300_DMA_Channel_TypeDef CH[8]; /* 0x000 .. 0x2B8 */
    volatile uint32_t RawTfr;       /* 0x2C0 */
    volatile uint32_t _R0;          /* 0x2C4 */
    volatile uint32_t RawBlock;     /* 0x2C8 */
    volatile uint32_t _R1;          /* 0x2CC */
    volatile uint32_t RawSrcTran;   /* 0x2D0 */
    volatile uint32_t _R2;          /* 0x2D4 */
    volatile uint32_t RawDstTran;   /* 0x2D8 */
    volatile uint32_t _R3;          /* 0x2DC */
    volatile uint32_t RawErr;       /* 0x2E0 */
    volatile uint32_t _R4;          /* 0x2E4 */
    volatile uint32_t StatusTfr;    /* 0x2E8 */
    volatile uint32_t _R5;          /* 0x2EC */
    volatile uint32_t StatusBlock;  /* 0x2F0 */
    volatile uint32_t _R6;          /* 0x2F4 */
    volatile uint32_t StatusSrcTran;/* 0x2F8 */
    volatile uint32_t _R7;          /* 0x2FC */
    volatile uint32_t StatusDstTran;/* 0x300 */
    volatile uint32_t _R8;          /* 0x304 */
    volatile uint32_t StatusErr;    /* 0x308 */
    volatile uint32_t _R9;          /* 0x30C */
    volatile uint32_t MaskTfr;      /* 0x310 */
    volatile uint32_t _R10;         /* 0x314 */
    volatile uint32_t MaskBlock;    /* 0x318 */
    volatile uint32_t _R11;         /* 0x31C */
    volatile uint32_t MaskSrcTran;  /* 0x320 */
    volatile uint32_t _R12;         /* 0x324 */
    volatile uint32_t MaskDstTran;  /* 0x328 */
    volatile uint32_t _R13;         /* 0x32C */
    volatile uint32_t MaskErr;      /* 0x330 */
    volatile uint32_t _R14;         /* 0x334 */
    volatile uint32_t ClearTfr;     /* 0x338 */
    volatile uint32_t _R15;         /* 0x33C */
    volatile uint32_t ClearBlock;   /* 0x340 */
    volatile uint32_t _R16;         /* 0x344 */
    volatile uint32_t ClearSrcTran; /* 0x348 */
    volatile uint32_t _R17;         /* 0x34C */
    volatile uint32_t ClearDstTran; /* 0x350 */
    volatile uint32_t _R18;         /* 0x354 */
    volatile uint32_t ClearErr;     /* 0x358 */
    volatile uint32_t _R19;         /* 0x35C */
    volatile uint32_t StatusInt;    /* 0x360 */
    volatile uint32_t _R20;         /* 0x364 */
    volatile uint32_t ReqSrcReg;    /* 0x368 */
    volatile uint32_t _R21;         /* 0x36C */
    volatile uint32_t ReqDstReg;    /* 0x370 */
    volatile uint32_t _R22;         /* 0x374 */
    volatile uint32_t SglReqSrcReg; /* 0x378 */
    volatile uint32_t _R23;         /* 0x37C */
    volatile uint32_t SglReqDstReg; /* 0x380 */
    volatile uint32_t _R24;         /* 0x384 */
    volatile uint32_t LstSrcReg;    /* 0x388 */
    volatile uint32_t _R25;         /* 0x38C */
    volatile uint32_t LstDstReg;    /* 0x390 */
    volatile uint32_t _R26;         /* 0x394 */
    volatile uint32_t DmaCfgReg;    /* 0x398 */
    volatile uint32_t _R27;         /* 0x39C */
    volatile uint32_t ChEnReg;      /* 0x3A0 */
    volatile uint32_t _R28;         /* 0x3A4 */
    volatile uint32_t DmaIdReg;     /* 0x3A8 */
    volatile uint32_t _R29;         /* 0x3AC */
    volatile uint32_t DmaTestReg;   /* 0x3B0 */
    volatile uint32_t _R30;         /* 0x3B4 */
} S300_DMA_TypeDef;

#define DMAC0 ((S300_DMA_TypeDef *)DMA0_BASE)
#define DMAC1 ((S300_DMA_TypeDef *)DMA1_BASE)

/* CTL_L bit positions (DW_ahb_dmac like) */
#define DMA_CTL_INT_EN_Pos       0
#define DMA_CTL_DST_TRW_Pos      1
#define DMA_CTL_SRC_TRW_Pos      4
#define DMA_CTL_DINC_Pos         7
#define DMA_CTL_SINC_Pos         9
#define DMA_CTL_DST_MSIZE_Pos    11
#define DMA_CTL_SRC_MSIZE_Pos    14
/* Enable gather/scatter features */
#define DMA_CTL_SRC_GATHER_EN_Pos 17
#define DMA_CTL_DST_SCATTER_EN_Pos 18
#define DMA_CTL_TT_FC_Pos        20
#define DMA_CTL_LLP_DST_EN_Pos   27
#define DMA_CTL_LLP_SRC_EN_Pos   28

/* CFG_L bit positions */
#define DMA_CFG_CH_PRIOR_Pos     5
#define DMA_CFG_MAX_ABRST_Pos    20
#define DMA_CFG_RELOAD_SRC_Pos   30
#define DMA_CFG_RELOAD_DST_Pos   31
/* Commonly used CFGL fields aligned to legacy driver */
#define DMA_CFGL_HS_SEL_DST_Pos  10
#define DMA_CFGL_HS_SEL_SRC_Pos  11
#define DMA_CFGL_DST_HS_POL_Pos  18
#define DMA_CFGL_SRC_HS_POL_Pos  19

/* CFG_H handshaking positions */
#define DMA_CFGH_SRC_PER_Pos     7
#define DMA_CFGH_DST_PER_Pos     11
/* Additional CFGH fields */
#define DMA_CFGH_FCMODE_Pos      0
#define DMA_CFGH_FIFOMODE_Pos    1
#define DMA_CFGH_PROTCTL_Pos     2

#ifdef __cplusplus
}
#endif

#endif /* S300_DMA_S300_H */
