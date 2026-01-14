#include "s300.h"
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "uart.h"
#include "w25qxx.h"

#define TAG "[S300] "

#define FLASH_ADDR_TARGET  0x00200000 // 2MB offset for Target Face
#define FLASH_ADDR_COMPARE 0x00300000 // 3MB offset for Compare Face
#define BUFFER_SIZE 4096
static uint8_t rx_buffer[BUFFER_SIZE];
static w25qxx_info_t w25qxx_info;

// Disable stdout to prevent debug output during data transfer
static void disable_stdout(void) {
    // Close and redirect stdout to prevent interference with data transfer
    fclose(stdout);
}

// Re-enable stdout after transfer
static void enable_stdout(void) {
    // Reinitialize stdout
    board_debug_uart_init();
}

// Helper to write buffer to flash (handling page boundaries)
static int flash_write_buffer(uint32_t addr, const uint8_t *buf, uint32_t len) {
    uint32_t written = 0;
    while (written < len) {
        uint32_t chunk = 256 - (addr & 0xFF); // Remaining space in current page
        if (chunk > (len - written)) {
            chunk = len - written;
        }
        if (w25qxx_write_page(addr, buf + written, chunk) != 0) {
            return -1;
        }
        addr += chunk;
        written += chunk;
    }
    return 0;
}

// Wait for UART TX to be completely empty (all data sent out)
static void uart_flush_tx(void) {
    // Wait for TEMT (Transmitter Empty) - both THR and shift register empty
    while (!(UART3->LSR & 0x40));
}

// Send ACK byte
static void uart_send_ack(void) {
    uart_flush_tx();
    UART3->RBR_THR_DLL = 'K';
    uart_flush_tx();
}

// Send NAK byte (error)
static void uart_send_nak(void) {
    uart_flush_tx();
    UART3->RBR_THR_DLL = 'E';
    uart_flush_tx();
}

// Receive one byte via polling
static uint8_t uart_recv_byte(void) {
    while (!(UART3->LSR & 1));  // Wait for DR (Data Ready)
    return UART3->RBR_THR_DLL;
}

// Receive a buffer via polling
static void uart_recv_buffer(uint8_t *buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = uart_recv_byte();
    }
}

int main(void)
{
    board_debug_uart_init(); /* UART3 */
    printf("\n" TAG "=== UART Face Transfer Demo ===\n");

    // Init Flash
    printf(TAG "Initializing Flash...\n");
    if (w25qxx_init(&w25qxx_info, true, false) != 0) {
        printf(TAG "Flash init failed!\n");
        while(1);
    }
    printf(TAG "Flash ID: %02X %02X %02X\n", w25qxx_info.manuf_id, w25qxx_info.memory_type, w25qxx_info.capacity);

    while (1) {
        // Protocol (no printf during transfer to avoid interference):
        // 1. Command (1 byte): 'T' (Target) or 'C' (Compare) -> ACK 'K'
        // 2. Size (4 bytes, LE) -> Erase flash -> ACK 'K'
        // 3. Data Chunks (4KB each) -> Write flash -> ACK 'K' per chunk
        // On error: NAK 'E'
        
        printf(TAG "Ready\n");
        fflush(stdout);
        uart_flush_tx();
        
        // Wait for command
        uint8_t cmd = uart_recv_byte();

        uint32_t current_addr = 0;
        const char *face_type = NULL;
        if (cmd == 'T') {
            current_addr = FLASH_ADDR_TARGET;
            face_type = "Target";
        } else if (cmd == 'C') {
            current_addr = FLASH_ADDR_COMPARE;
            face_type = "Compare";
        } else {
            continue; // Ignore invalid command
        }
        
        // ACK command
        uart_send_ack();

        // Receive size (4 bytes)
        uint32_t file_size = 0;
        uart_recv_buffer((uint8_t*)&file_size, 4);

        // Erase flash sectors
        uint32_t sectors = (file_size + 4095) / 4096;
        for (uint32_t i = 0; i < sectors; i++) {
            w25qxx_erase_4k(current_addr + i * 4096);
        }

        // ACK erase complete
        uart_send_ack();

        // Disable stdout before data transfer to prevent debug output
        disable_stdout();

        // Receive data
        uint32_t received = 0;
        uint32_t write_addr = current_addr;
        int error = 0;

        while (received < file_size) {
            uint32_t chunk_size = file_size - received;
            if (chunk_size > BUFFER_SIZE) {
                chunk_size = BUFFER_SIZE;
            }
            
            // Receive chunk via polling
            uart_recv_buffer(rx_buffer, chunk_size);

            // Write to flash
            if (flash_write_buffer(write_addr, rx_buffer, chunk_size) != 0) {
                error = 1;
                uart_send_nak();
                break;
            }

            received += chunk_size;
            write_addr += chunk_size;
            
            // ACK this chunk
            uart_send_ack();
        }

        // Re-enable stdout after data transfer
        enable_stdout();
        
        // Print result only after transfer complete
        if (!error) {
            printf(TAG "%s face saved: %u bytes @ 0x%08X\n", face_type, (unsigned)received, (unsigned)current_addr);
        } else {
            printf(TAG "Transfer failed\n");
        }
    }
}
