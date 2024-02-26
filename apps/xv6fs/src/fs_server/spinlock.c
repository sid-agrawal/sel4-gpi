// Mutual exclusion spin locks.

#include <defs.h>
#include <spinlock.h>
#include <proc.h>

// ARYA-TODO implement real locks?

void initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void acquire(struct spinlock *lk)
{
}

// Release the lock.
void release(struct spinlock *lk)
{
}
