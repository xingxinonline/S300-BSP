.syntax unified
.cpu cortex-m4
.fpu fpv4-sp-d16
.thumb

.extern SystemInit
.extern main

/* Linker provides _estack */

.section .isr_vector,"a",%progbits
.global g_pfnVectors
.type g_pfnVectors, %object
g_pfnVectors:
    /* Vector table: core exceptions + a subset of device IRQs (expand as needed) */
    .word _estack
    .word Reset_Handler
    .word NMI_Handler
    .word HardFault_Handler
    .word MemManage_Handler
    .word BusFault_Handler
    .word UsageFault_Handler
    .word 0
    .word 0
    .word 0
    .word 0
    .word SVC_Handler
    .word DebugMon_Handler
    .word 0
    .word PendSV_Handler
    .word SysTick_Handler
     /* Device IRQs start here (index 0 corresponds to first device IRQ)
         Reordered to follow cortex-m4-bsp/driver/interrupt.h emINUM definition */
     .word TIMER0_IRQHandler     /* 0  EM_INUM_TIMER0 */
     .word TIMER1_IRQHandler     /* 1  EM_INUM_TIMER1 */
     .word TIMER2_IRQHandler     /* 2  EM_INUM_TIMER2 */
     .word TIMER3_IRQHandler     /* 3  EM_INUM_TIMER3 */
     .word TIMER4_IRQHandler     /* 4  EM_INUM_TIMER4 */
     .word TIMER5_IRQHandler     /* 5  EM_INUM_TIMER5 */
     .word WDT0_IRQHandler       /* 6  EM_INUM_WDT0 */
     .word WDT1_IRQHandler       /* 7  EM_INUM_WDT1 */
     .word WDT2_IRQHandler       /* 8  EM_INUM_WDT2 */
     .word WDT3_IRQHandler       /* 9  EM_INUM_WDT3 */
     .word N_INTR_IRQHandler     /* 10 EM_INUM_N */
     .word S_INTR_IRQHandler     /* 11 EM_INUM_S */
     .word FLASH_IRQHandler      /* 12 EM_INUM_FLASH */
     .word UART0_IRQHandler      /* 13 EM_INUM_UART0 */
     .word UART1_IRQHandler      /* 14 EM_INUM_UART1 */
     .word UART2_IRQHandler      /* 15 EM_INUM_UART2 */
     .word UART3_IRQHandler      /* 16 EM_INUM_UART3 */
     .word I2C0_IRQHandler       /* 17 EM_INUM_I2C0 */
     .word I2C1_IRQHandler       /* 18 EM_INUM_I2C1 */
     .word I2C2_IRQHandler       /* 19 EM_INUM_I2C2 */
     .word I2C3_IRQHandler       /* 20 EM_INUM_I2C3 */
     .word GPIO_IRQHandler       /* 21 EM_INUM_GPIO */
     .word MAIL_IRQHandler       /* 22 EM_INUM_MAIL */
     .word I2S0_IRQHandler       /* 23 EM_INUM_I2S0 */
     .word I2S1_IRQHandler       /* 24 EM_INUM_I2S1 */
     .word PWM_IRQHandler        /* 25 EM_INUM_PWM */
     .word GMAC_IRQHandler       /* 26 EM_INUM_GMAC */
     .word DMA0_IRQHandler       /* 27 EM_INUM_DMA0 */
     .word DMA1_IRQHandler       /* 28 EM_INUM_DMA1 */
     .word SPI0_IRQHandler       /* 29 EM_INUM_SPI0 */
     .word SPI1_IRQHandler       /* 30 EM_INUM_SPI1 */
     .word LCD_IRQHandler        /* 31 EM_INUM_LCD */
     .word OV5640_IRQHandler     /* 32 EM_INUM_OV5640 */
     .word SDIO0_IRQHandler      /* 33 EM_INUM_SDIO0 */
     .word SDIO1_IRQHandler      /* 34 EM_INUM_SDIO1 */
     .word AON_GPIO_IRQHandler   /* 35 EM_INUM_AON_GPIO */
     .word AON_I2S1_IRQHandler   /* 36 EM_INUM_AON_I2S1 */
     .word AON_I2S0_IRQHandler   /* 37 EM_INUM_AON_I2S0 */
     .word WDT_IRQHandler        /* 38 EM_INUM_WDT */
     .word RTC_IRQHandler        /* 39 EM_INUM_RTC */
.size g_pfnVectors, .-g_pfnVectors

/* Code section */
.section .text.Reset_Handler,"ax",%progbits
.global Reset_Handler
.thumb_func
Reset_Handler:
    /* Zero initialize .bss */
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
1:
    cmp r0, r1
    bcc 2f
    b 3f
2:
    str r2, [r0], #4
    b 1b
3:
    ldr r0, =SystemInit
    blx r0
    ldr r0, =main
    blx r0
    b .

.section .text.DefaultHandlers,"ax",%progbits
.weak NMI_Handler
.thumb_func
NMI_Handler:
    b .

.weak HardFault_Handler
.thumb_func
HardFault_Handler:
    b .

.weak MemManage_Handler
.thumb_func
MemManage_Handler:
    b .

.weak BusFault_Handler
.thumb_func
BusFault_Handler:
    b .

.weak UsageFault_Handler
.thumb_func
UsageFault_Handler:
    b .

.weak SVC_Handler
.thumb_func
SVC_Handler:
    b .

.weak DebugMon_Handler
.thumb_func
DebugMon_Handler:
    b .

.weak PendSV_Handler
.thumb_func
PendSV_Handler:
    b .

.weak SysTick_Handler
.thumb_func
SysTick_Handler:
    b .

/* Weak device IRQ handlers */
.weak TIMER0_IRQHandler
.thumb_func
TIMER0_IRQHandler: b .
.weak TIMER1_IRQHandler
.thumb_func
TIMER1_IRQHandler: b .
.weak TIMER2_IRQHandler
.thumb_func
TIMER2_IRQHandler: b .
.weak TIMER3_IRQHandler
.thumb_func
TIMER3_IRQHandler: b .
.weak TIMER4_IRQHandler
.thumb_func
TIMER4_IRQHandler: b .
.weak TIMER5_IRQHandler
.thumb_func
TIMER5_IRQHandler: b .
.weak WDT0_IRQHandler
.thumb_func
WDT0_IRQHandler: b .
.weak WDT1_IRQHandler
.thumb_func
WDT1_IRQHandler: b .
.weak WDT2_IRQHandler
.thumb_func
WDT2_IRQHandler: b .
.weak WDT3_IRQHandler
.thumb_func
WDT3_IRQHandler: b .
.weak N_INTR_IRQHandler
.thumb_func
N_INTR_IRQHandler: b .
.weak S_INTR_IRQHandler
.thumb_func
S_INTR_IRQHandler: b .
.weak FLASH_IRQHandler
.thumb_func
FLASH_IRQHandler: b .
.weak MAIL_IRQHandler
.thumb_func
MAIL_IRQHandler: b .
.weak LCD_IRQHandler
.thumb_func
LCD_IRQHandler: b .
.weak OV5640_IRQHandler
.thumb_func
OV5640_IRQHandler: b .
.weak UART0_IRQHandler
.thumb_func
UART0_IRQHandler: b .
.weak UART1_IRQHandler
.thumb_func
UART1_IRQHandler: b .
.weak UART2_IRQHandler
.thumb_func
UART2_IRQHandler: b .
.weak UART3_IRQHandler
.thumb_func
UART3_IRQHandler: b .
.weak I2C0_IRQHandler
.thumb_func
I2C0_IRQHandler: b .
.weak I2C1_IRQHandler
.thumb_func
I2C1_IRQHandler: b .
.weak I2C2_IRQHandler
.thumb_func
I2C2_IRQHandler: b .
.weak I2C3_IRQHandler
.thumb_func
I2C3_IRQHandler: b .
.weak GPIO_IRQHandler
.thumb_func
GPIO_IRQHandler: b .
.weak I2S0_IRQHandler
.thumb_func
I2S0_IRQHandler: b .
.weak I2S1_IRQHandler
.thumb_func
I2S1_IRQHandler: b .
.weak PWM_IRQHandler
.thumb_func
PWM_IRQHandler: b .
.weak DMA0_IRQHandler
.thumb_func
DMA0_IRQHandler: b .
.weak DMA1_IRQHandler
.thumb_func
DMA1_IRQHandler: b .
.weak SPI0_IRQHandler
.thumb_func
SPI0_IRQHandler: b .
.weak SPI1_IRQHandler
.thumb_func
SPI1_IRQHandler: b .
.weak SSI0_IRQHandler
.thumb_func
SSI0_IRQHandler: b .
.weak SSI1_IRQHandler
.thumb_func
SSI1_IRQHandler: b .
.weak SDIO0_IRQHandler
.thumb_func
SDIO0_IRQHandler: b .
.weak SDIO1_IRQHandler
.thumb_func
SDIO1_IRQHandler: b .
.weak AON_GPIO_IRQHandler
.thumb_func
AON_GPIO_IRQHandler: b .
.weak AON_I2S0_IRQHandler
.thumb_func
AON_I2S0_IRQHandler: b .
.weak AON_I2S1_IRQHandler
.thumb_func
AON_I2S1_IRQHandler: b .
.weak GMAC_IRQHandler
.thumb_func
GMAC_IRQHandler: b .
.weak ETH_IRQHandler
.thumb_func
ETH_IRQHandler: b .
.weak WDT_IRQHandler
.thumb_func
WDT_IRQHandler: b .
.weak WDT_RST_IRQHandler
.thumb_func
WDT_RST_IRQHandler: b .
.weak RTC_IRQHandler
.thumb_func
RTC_IRQHandler: b .
