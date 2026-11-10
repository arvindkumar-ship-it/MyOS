#pragma once

#include <stdint.h>
#include <stdbool.h>

// Fixed-size byte queue (first in, first out).
// One writer and one reader. On a single CPU this is safe when the writer is an
// interrupt handler and the reader disables interrupts while it pops.

#define RINGBUF_SIZE 1024   // must be a power of two (we use & instead of %)

struct ringbuf {
    uint8_t data[RINGBUF_SIZE];
    volatile uint32_t head;   // next index to write (only the writer changes it)
    volatile uint32_t tail;   // next index to read  (only the reader changes it)
};

// head and tail only grow; their difference is the number of bytes stored.
// Unsigned overflow wraps around, and the difference stays correct.
static inline uint32_t ringbuf_count(const struct ringbuf *rb) {
    return rb->head - rb->tail;
}

// Returns false (and drops the byte) if the buffer is full.
static inline bool ringbuf_push(struct ringbuf *rb, uint8_t byte) {
    if (ringbuf_count(rb) == RINGBUF_SIZE) {
        return false;
    }
    rb->data[rb->head & (RINGBUF_SIZE - 1)] = byte;
    rb->head++;
    return true;
}

static inline bool ringbuf_pop(struct ringbuf *rb, uint8_t *byte) {
    if (ringbuf_count(rb) == 0) {
        return false;
    }
    *byte = rb->data[rb->tail & (RINGBUF_SIZE - 1)];
    rb->tail++;
    return true;
}
