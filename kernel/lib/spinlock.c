#include "lib/spinlock.h"

#include "lib/panic.h"

void spin_lock_deadlock(struct spinlock *lock) {
    panic("deadlock: spinlock '%s' is already held (single CPU, interrupts off: "
          "the holder can never release it)", lock->name);
}
