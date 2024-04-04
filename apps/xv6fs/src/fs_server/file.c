//
// Support functions for system calls that involve file descriptors.
//

#include <stdlib.h>
#include <defs.h>
#include <file.h>
#include <proc.h>

struct devsw devsw[NDEV];

// Allocate a file structure.
struct file *
filealloc(void)
{
  printf("TEMPA huh\n");
  struct file *f = malloc(sizeof(struct file));
  printf("TEMPA huh 1\n");

  if (f == NULL)
  {
    printf("TEMPA huh 2\n");
    return NULL;
  }

  printf("TEMPA huh 3\n");
  f->ref = 1;
  printf("TEMPA huh 4\n");
  return f;
}

// Increment ref count for file f.
struct file *
filedup(struct file *f)
{
  if (f->ref < 1)
    xv6fs_panic("filedup");
  f->ref++;
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void fileclose(struct file *f)
{
  struct file ff;

  if (f->ref < 1)
    xv6fs_panic("fileclose");
  if (--f->ref > 0)
  {
    return;
  }

  free(f);

  if (ff.type == FD_INODE || ff.type == FD_DEVICE)
  {
    iput(ff.ip);
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int filestat(struct file *f, uint64_t addr)
{
  struct proc *p = myproc();
  struct stat st;

  if (f->type == FD_INODE || f->type == FD_DEVICE)
  {
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int fileread(struct file *f, uint64_t addr, int n)
{
  int r = 0;

  if (f->readable == 0)
    return -1;

  if (f->type == FD_DEVICE)
  {
    if (f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  }
  else if (f->type == FD_INODE)
  {
    ilock(f->ip);
    if ((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  }
  else
  {
    xv6fs_panic("fileread");
  }

  return r;
}

// Writes all blocknos for file f in the buf
// If the buf runs out of size, stops writing
int file_blocknos(struct file *f, int *buf, int buf_size, int *result_size)
{
  if (f->type == FD_INODE)
  {
    ilock(f->ip);
    int i = 0;
    for (uint32_t block_offset = 0; block_offset < f->ip->size / BSIZE; block_offset += 1) {
      int blockno = bmap_noalloc(f->ip, block_offset);

      if (blockno != 0) {
        if (i < buf_size) {
          buf[i] = blockno;
          i++;
        } else {
          *result_size = i;
          iunlock(f->ip);
          return -1;
        }
      }
    }
    *result_size = i;
    iunlock(f->ip);
  }
  else
  {
    xv6fs_panic("file_blocknos");
  }

}

// Write to file f.
// addr is a user virtual address.
int filewrite(struct file *f, uint64_t addr, int n)
{
  int r, ret = 0;

  if (f->writable == 0)
    return -1;

  if (f->type == FD_DEVICE)
  {
    if (f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  }
  else if (f->type == FD_INODE)
  {
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
    int i = 0;
    while (i < n)
    {
      int n1 = n - i;
      if (n1 > max)
        n1 = max;

      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);

      if (r != n1)
      {
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  }
  else
  {
    xv6fs_panic("filewrite");
  }

  return ret;
}
