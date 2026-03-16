#include "xm04_ring_buffer.h"

#define XM04_RING_BUFFER_MASK (XM04_RING_BUFFER_SIZE - 1u)

void xm04_ring_buffer_init(xm04_ring_buffer_t *buffer)
{
    if (buffer == 0) {
        return;
    }

    buffer->head = 0;
    buffer->tail = 0;
    buffer->dropped = 0;
}

void xm04_ring_buffer_reset(xm04_ring_buffer_t *buffer)
{
    if (buffer == 0) {
        return;
    }

    buffer->head = 0;
    buffer->tail = 0;
}

bool xm04_ring_buffer_push(xm04_ring_buffer_t *buffer, uint8_t value)
{
    uint16_t next_head;

    if (buffer == 0) {
        return false;
    }

    next_head = (uint16_t)((buffer->head + 1u) & XM04_RING_BUFFER_MASK);
    if (next_head == buffer->tail) {
        buffer->dropped++;
        return false;
    }

    buffer->data[buffer->head] = value;
    buffer->head = next_head;
    return true;
}

bool xm04_ring_buffer_pop(xm04_ring_buffer_t *buffer, uint8_t *value)
{
    if ((buffer == 0) || (value == 0)) {
        return false;
    }

    if (buffer->head == buffer->tail) {
        return false;
    }

    *value = buffer->data[buffer->tail];
    buffer->tail = (uint16_t)((buffer->tail + 1u) & XM04_RING_BUFFER_MASK);
    return true;
}

uint16_t xm04_ring_buffer_available(const xm04_ring_buffer_t *buffer)
{
    if (buffer == 0) {
        return 0;
    }

    return (uint16_t)((buffer->head - buffer->tail) & XM04_RING_BUFFER_MASK);
}