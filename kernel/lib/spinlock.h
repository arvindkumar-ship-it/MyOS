#pragma once

#include <stdbool.h>
#include "arch/x86_64/cpu.h"

// Spinlock: protects data shared between tasks and interrupt handlers.
//
// spin_lock() first disables interrupts (so neither the timer nor any other
// IRQ can switch tasks or run a handler that takes the same lock), then
// atomically sets "locked". On a multi-CPU system another CPU could hold the
// lock, so we would spin (wait in a loop). MyOS runs on one CPU: if the lock
// is already held when we get here, the holder can never run again to release
// it (interrupts are off), so that is a deadlock and we panic right away.

struct spinlock {
    volatile int locked;
    const char *name;        // for error messages
    bool irq_was_enabled;    // interrupt state before spin_lock (restored by unlock)
};

#define SPINLOCK_INIT(n) { .locked = 0, .name = (n), .irq_was_enabled = false }

void spin_lock_deadlock(struct spinlock *lock) __attribute__((noreturn));

static inline void spin_lock(struct spinlock *lock) {
    bool was_enabled = cpu_irq_save();
    // __atomic_exchange_n compiles to "xchg": store 1 and return the old value
    // in one indivisible step.
    while (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE) != 0) {
        spin_lock_deadlock(lock);   // single CPU: waiting would never end
    }
    lock->irq_was_enabled = was_enabled;
}

static inline void spin_unlock(struct spinlock *lock) {
    bool was_enabled = lock->irq_was_enabled;
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
    cpu_irq_restore(was_enabled);
}
