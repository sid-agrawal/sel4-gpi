// Mutual exclusion spin locks.

#include <xv6fs/defs.h>
#include <xv6fs/spinlock.h>
#include <xv6fs/proc.h>

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void
acquire(struct spinlock *lk)
{
  // ARYA-TODO implement real locks?
}

// Release the lock.
void
release(struct spinlock *lk)
{
  // ARYA-TODO implement real locks?
}

void
wakeup(void* addr) {
}

void
xv6fs_sleep(void* addr, struct spinlock* lk) {
}
