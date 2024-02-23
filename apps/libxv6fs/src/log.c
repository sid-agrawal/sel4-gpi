#include <string.h>
#include <xv6fs/defs.h>
#include <xv6fs/spinlock.h>
#include <xv6fs/sleeplock.h>
#include <xv6fs/fs.h>
#include <xv6fs/buf.h>

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader
{
  int n;
  int block[LOGSIZE];
};

struct xv6fs_log
{
  struct spinlock lock;
  int start;
  int size;
  int outstanding; // how many FS sys calls are executing.
  int committing;  // in commit(), please wait.
  int dev;
  struct logheader lh;
};
struct xv6fs_log xv6fs_log;

static void recover_from_log(void);
static void commit();

void initlog(int dev, struct superblock *sb)
{
  if (sizeof(struct logheader) >= BSIZE)
    xv6fs_panic("initlog: too big logheader");

  initlock(&xv6fs_log.lock, "log");
  xv6fs_log.start = sb->logstart;
  xv6fs_log.size = sb->nlog;
  xv6fs_log.dev = dev;
  // ARYA-TODO do we need log recovery?
  // recover_from_log();
}

// Copy committed blocks from log to their home location
static void
install_trans(int recovering)
{
  int tail;

  for (tail = 0; tail < xv6fs_log.lh.n; tail++)
  {
    struct buf *lbuf = bread(xv6fs_log.dev, xv6fs_log.start + tail + 1); // read log block
    struct buf *dbuf = bread(xv6fs_log.dev, xv6fs_log.lh.block[tail]);   // read dst
    memmove(dbuf->data, lbuf->data, BSIZE);                              // copy block to dst
    bwrite(dbuf);                                                        // write dst to disk
    if (recovering == 0)
      bunpin(dbuf);
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
static void
read_head(void)
{
  struct buf *buf = bread(xv6fs_log.dev, xv6fs_log.start);
  struct logheader *lh = (struct logheader *)(buf->data);
  int i;
  xv6fs_log.lh.n = lh->n;
  for (i = 0; i < xv6fs_log.lh.n; i++)
  {
    xv6fs_log.lh.block[i] = lh->block[i];
  }
  brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
static void
write_head(void)
{
  struct buf *buf = bread(xv6fs_log.dev, xv6fs_log.start);
  struct logheader *hb = (struct logheader *)(buf->data);
  int i;
  hb->n = xv6fs_log.lh.n;
  for (i = 0; i < xv6fs_log.lh.n; i++)
  {
    hb->block[i] = xv6fs_log.lh.block[i];
  }
  bwrite(buf);
  brelse(buf);
}

static void
recover_from_log(void)
{
  read_head();
  install_trans(1); // if committed, copy from log to disk
  xv6fs_log.lh.n = 0;
  write_head(); // clear the log
}

// called at the start of each FS system call.
void begin_op(void)
{
  acquire(&xv6fs_log.lock);
  while (1)
  {
    if (xv6fs_log.committing)
    {
      xv6fs_sleep(&xv6fs_log, &xv6fs_log.lock);
    }
    else if (xv6fs_log.lh.n + (xv6fs_log.outstanding + 1) * MAXOPBLOCKS > LOGSIZE)
    {
      // this op might exhaust log space; wait for commit.
      xv6fs_sleep(&xv6fs_log, &xv6fs_log.lock);
    }
    else
    {
      xv6fs_log.outstanding += 1;
      release(&xv6fs_log.lock);
      break;
    }
  }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void end_op(void)
{
  int do_commit = 0;

  acquire(&xv6fs_log.lock);
  xv6fs_log.outstanding -= 1;
  if (xv6fs_log.committing)
    xv6fs_panic("log.committing");
  if (xv6fs_log.outstanding == 0)
  {
    do_commit = 1;
    xv6fs_log.committing = 1;
  }
  else
  {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&xv6fs_log);
  }
  release(&xv6fs_log.lock);

  if (do_commit)
  {
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&xv6fs_log.lock);
    xv6fs_log.committing = 0;
    wakeup(&xv6fs_log);
    release(&xv6fs_log.lock);
  }
}

// Copy modified blocks from cache to log.
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < xv6fs_log.lh.n; tail++)
  {
    struct buf *to = bread(xv6fs_log.dev, xv6fs_log.start + tail + 1); // log block
    struct buf *from = bread(xv6fs_log.dev, xv6fs_log.lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE);
    bwrite(to); // write the log
    brelse(from);
    brelse(to);
  }
}

static void
commit()
{
  if (xv6fs_log.lh.n > 0)
  {
    write_log();      // Write modified blocks from cache to log
    write_head();     // Write header to disk -- the real commit
    install_trans(0); // Now install writes to home locations
    xv6fs_log.lh.n = 0;
    write_head(); // Erase the transaction from the log
  }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache by increasing refcnt.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void log_write(struct buf *b)
{
  int i;

  acquire(&xv6fs_log.lock);
  if (xv6fs_log.lh.n >= LOGSIZE || xv6fs_log.lh.n >= xv6fs_log.size - 1)
    xv6fs_panic("too big a transaction");
  if (xv6fs_log.outstanding < 1)
    xv6fs_panic("log_write outside of trans");

  for (i = 0; i < xv6fs_log.lh.n; i++)
  {
    if (xv6fs_log.lh.block[i] == b->blockno) // log absorption
      break;
  }
  xv6fs_log.lh.block[i] = b->blockno;
  if (i == xv6fs_log.lh.n)
  { // Add new block to log?
    bpin(b);
    xv6fs_log.lh.n++;
  }
  release(&xv6fs_log.lock);
}
