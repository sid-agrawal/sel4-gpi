// Sleeping locks

#include <xv6fs/defs.h>
#include <xv6fs/spinlock.h>
#include <xv6fs/sleeplock.h>
#include <xv6fs/proc.h>

// ARYA-TODO implement real locks?

void
initsleeplock(struct sleeplock *lk, char *name)
{
  initlock(&lk->lk, "sleep lock");
  lk->name = name;
  lk->locked = 0;
  lk->pid = 0;
}

void
acquiresleep(struct sleeplock *lk)
{
}

void
releasesleep(struct sleeplock *lk)
{
}

int
holdingsleep(struct sleeplock *lk)
{
  return 1;
}



