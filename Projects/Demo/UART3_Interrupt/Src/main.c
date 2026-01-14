#include "s300.h"
#include <stdio.h>
#include <stdbool.h>
#include "rcc.h"
#include "gpio.h"
#include "uart.h"
#include "board.h"

#define UART_TEST_IDX UART_IDX3

/* Buffer for received data */
#define RX_BUFFER_SIZE 128
static volatile uint8_t rx_buffer[RX_BUFFER_SIZE];
static volatile uint32_t rx_head = 0;
static volatile uint32_t rx_tail = 0;

static void init_uart3_pins(void)
{
    /* GPIOA pin26/pin27 -> FUNCTION_3 per legacy demo */
    set_cortex_m4_apb1_clock(RCC_CM4_APB1_GPIO, true);
    set_gpio_function(GPIOA, 26, FUNCTION_3);
    set_gpio_function(GPIOA, 27, FUNCTION_3);
}

void UART3_IRQHandler(void)
{
    S300_UART_TypeDef *U = UART3;
    uint32_t iir = U->IIR_FCR;
    uint32_t int_id = iir & 0x0F;
    
    /* 
       IIR Interrupt ID:
       0100 (4) = Received Data Available
       1100 (12) = Character Timeout (FIFO)
       0110 (6) = Receiver Line Status
    */
    
    if ((int_id == 0x04) || (int_id == 0x0C))
    {
        /* Read data while Data Ready (LSR bit 0) is set */
        while (U->LSR & 0x01)
        {
            uint8_t data = (uint8_t)(U->RBR_THR_DLL & 0xFF);
            
            uint32_t next_head = (rx_head + 1) % RX_BUFFER_SIZE;
            if (next_head != rx_tail)
            {
                rx_buffer[rx_head] = data;
                rx_head = next_head;
            }
            else
            {
                /* Buffer overflow, discard data */
            }
        }
    }
    else if (int_id == 0x06)
    {
        /* Receiver Line Status Interrupt */
        /* Reading LSR clears this interrupt */
        volatile uint32_t lsr = U->LSR;
        (void)lsr;
    }
}

int main(void)
{
    board_debug_uart_init();
    printf("UART3 Interrupt Demo Start\n");

    set_cortex_m4_apb1_clock(RCC_CM4_APB1_UART3, true);
    
    /* Initialize UART3: 115200 8N1 */
    init_uart(UART_TEST_IDX, UARTTYPE_STD_SERIAL, rcc_get_clock(RCC_CLOCK_APB1), 115200);
    init_uart3_pins();
    
    /* Enable RX interrupt */
    set_uart_interrupt(UART_TEST_IDX, false, true);
    
    /* Enable NVIC */
    NVIC_ClearPendingIRQ(UART3_IRQn);
    NVIC_SetPriority(UART3_IRQn, 3);
    NVIC_EnableIRQ(UART3_IRQn);
    
    printf("UART3 Initialized. Send characters to UART3...\n");

    for (;;)
    {
        /* Process received data */
        if (rx_head != rx_tail)
        {
            uint8_t data = rx_buffer[rx_tail];
            rx_tail = (rx_tail + 1) % RX_BUFFER_SIZE;
            
            /* Echo back using polling write */
            write_uart(UART_TEST_IDX, UARTTYPE_STD_SERIAL, data);
            
            /* Also print to debug console */
            printf("Rx: %c (0x%02X)\n", data, data);
        }
    }
}
