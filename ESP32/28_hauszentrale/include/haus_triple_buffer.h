/**
 * @file haus_triple_buffer.h
 * @brief Lock-free single-producer / single-reader triple buffer.
 *
 * C++ only. The producer owns one private slot, the reader owns one slot, and
 * the third slot sits in the middle. Publishing exchanges the producer's slot
 * with the middle slot using a release store; acquiring exchanges the reader's
 * slot with the middle using an acquire load. The reader therefore always gets
 * a block the writer cannot touch until the next exchange.
 *
 * Readers must be serialised externally (the UI and recorder share one
 * read-side mutex); the producer never takes that lock.
 */

#ifndef HAUS_TRIPLE_BUFFER_H
#define HAUS_TRIPLE_BUFFER_H

#include <atomic>
#include <cstdint>

template <typename T>
struct TripleBuffer {
    T slot[3];
    std::atomic<uint8_t> middle{0};
    uint8_t write_slot = 1;
    uint8_t read_slot = 2;

    /* Producer's private slot. Write every owned field, then publish(). */
    T &write() { return slot[write_slot]; }

    /* Producer: ~1 Hz. */
    void publish() {
        write_slot = middle.exchange(write_slot, std::memory_order_release);
    }

    /* Reader: copy out immediately. Do not retain the reference. */
    const T &acquire() {
        read_slot = middle.exchange(read_slot, std::memory_order_acquire);
        return slot[read_slot];
    }
};

#endif /* HAUS_TRIPLE_BUFFER_H */
