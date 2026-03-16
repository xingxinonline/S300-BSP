#ifndef XM04_RING_BUFFER_H
#define XM04_RING_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

#ifndef XM04_RING_BUFFER_SIZE
#define XM04_RING_BUFFER_SIZE 128u
#endif

typedef struct {
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint32_t dropped;
    uint8_t data[XM04_RING_BUFFER_SIZE];
} xm04_ring_buffer_t;

void xm04_ring_buffer_init(xm04_ring_buffer_t *buffer);
void xm04_ring_buffer_reset(xm04_ring_buffer_t *buffer);
bool xm04_ring_buffer_push(xm04_ring_buffer_t *buffer, uint8_t value);
bool xm04_ring_buffer_pop(xm04_ring_buffer_t *buffer, uint8_t *value);
uint16_t xm04_ring_buffer_available(const xm04_ring_buffer_t *buffer);

#endif