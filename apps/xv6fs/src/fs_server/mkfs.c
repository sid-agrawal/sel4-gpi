#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define stat xv6_stat // avoid clash with host struct stat
#include <defs.h>
#include <fs.h>

#ifndef static_assert
#define static_assert(a, b) \
  do                        \
  {                         \
    switch (0)              \
    case 0:                 \
    case (a):;              \
  } while (0)
#endif

// Disk layout:
// [ boot block | sb block | log | inode blocks | free bit map | data blocks ]

int nbitmap = FS_SIZE / (BSIZE * 8) + 1;
int ninodeblocks = N_INODES / IPB + 1;
int nlog = LOGSIZE;
int nmeta;   // Number of meta blocks (boot, sb, nlog, inode, bitmap)
int nblocks; // Number of data blocks

int fsfd;
static struct superblock sb;
char zeroes[BSIZE];
uint32_t freeinode = 1;
uint32_t freeblock;

void mkfs_balloc(int);
void wsect(uint32_t, void *);
void winode(uint32_t, struct dinode *);
void rinode(uint32_t inum, struct dinode *ip);
void rsect(uint32_t sec, void *buf);
uint32_t mkfs_ialloc(uint16_t type);
void iappend(uint32_t inum, void *p, int n);

// convert to riscv byte order
uint16_t
xshort(uint16_t x)
{
  uint16_t y;
  uint8_t *a = (uint8_t *)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint32_t xint(uint32_t x)
{
  uint32_t y;
  uint8_t *a = (uint8_t *)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int init_disk_file(void)
{
  int i, cc, fd;
  uint32_t rootino, inum, off;
  struct dirent de;
  char buf[BSIZE];
  struct dinode din;

  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);

  // 1 fs block = 1 disk sector
  nmeta = 2 + nlog + ninodeblocks + nbitmap;
  nblocks = FS_SIZE - nmeta;

  sb.magic = FSMAGIC;
  sb.size = xint(FS_SIZE);
  sb.nblocks = xint(nblocks);
  sb.ninodes = xint(N_INODES);
  sb.nlog = xint(nlog);
  sb.logstart = xint(2);
  sb.inodestart = xint(2 + nlog);
  sb.bmapstart = xint(2 + nlog + ninodeblocks);

  printf("nmeta %d (boot, super, log blocks %u inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         nmeta, nlog, ninodeblocks, nbitmap, nblocks, FS_SIZE);

  freeblock = nmeta; // the first free block that we can allocate

  for (i = 0; i < FS_SIZE; i++)
    wsect(i, zeroes);

  memset(buf, 0, sizeof(buf));
  memmove(buf, &sb, sizeof(sb));
  wsect(1, buf);

  rootino = mkfs_ialloc(T_DIR);
  assert(rootino == ROOTINO);

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  // fix size of root inode dir
  rinode(rootino, &din);
  off = xint(din.size);
  off = ((off / BSIZE) + 1) * BSIZE;
  din.size = xint(off);
  winode(rootino, &din);

  mkfs_balloc(freeblock);

  return 0;
}

void wsect(uint32_t sec, void *buf)
{
  xv6fs_bwrite(sec, buf);
}

void winode(uint32_t inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint32_t bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode *)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

void rinode(uint32_t inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint32_t bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode *)buf) + (inum % IPB);
  *ip = *dip;
}

void rsect(uint32_t sec, void *buf)
{
  xv6fs_bread(sec, buf);
}

uint32_t mkfs_ialloc(uint16_t type)
{
  uint32_t inum = freeinode++;
  struct dinode din;

  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);
  winode(inum, &din);
  return inum;
}

void mkfs_balloc(int used)
{
  uint8_t buf[BSIZE];
  int i;

  printf("mkfs_balloc: first %d blocks have been allocated\n", used);
  assert(used < BSIZE * 8);
  bzero(buf, BSIZE);
  for (i = 0; i < used; i++)
  {
    buf[i / 8] = buf[i / 8] | (0x1 << (i % 8));
  }
  printf("mkfs_balloc: write bitmap block at sector %d\n", sb.bmapstart);
  wsect(sb.bmapstart, buf);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

void iappend(uint32_t inum, void *xp, int n)
{
  char *p = (char *)xp;
  uint32_t fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint32_t indirect[NINDIRECT];
  uint32_t x;

  rinode(inum, &din);
  off = xint(din.size);
  // printf("append inum %d at off %d sz %d\n", inum, off, n);
  while (n > 0)
  {
    fbn = off / BSIZE;
    assert(fbn < MAXFILE);
    if (fbn < NDIRECT)
    {
      if (xint(din.addrs[fbn]) == 0)
      {
        din.addrs[fbn] = xint(freeblock++);
      }
      x = xint(din.addrs[fbn]);
    }
    else
    {
      if (xint(din.addrs[NDIRECT]) == 0)
      {
        din.addrs[NDIRECT] = xint(freeblock++);
      }
      rsect(xint(din.addrs[NDIRECT]), (char *)indirect);
      if (indirect[fbn - NDIRECT] == 0)
      {
        indirect[fbn - NDIRECT] = xint(freeblock++);
        wsect(xint(din.addrs[NDIRECT]), (char *)indirect);
      }
      x = xint(indirect[fbn - NDIRECT]);
    }
    n1 = min(n, (fbn + 1) * BSIZE - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * BSIZE), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
}