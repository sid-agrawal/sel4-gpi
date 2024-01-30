#pragma once

struct file
{
  enum
  {
    FD_NONE,
    FD_PIPE,
    FD_INODE,
    FD_DEVICE
  } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
};

// in-memory copy of an inode
struct inode
{
  // ARYA-TODO we have no mode / permissions / ownership, does that matter?

  uint dev;              // Device number
  uint inum;             // Inode number
  int ref;               // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;             // inode has been read from disk?
  short type;
  short major;
  short minor;
  short nlink;
  uint size;
  int ctime;
  int atime;
  int mtime;
  uint addrs[NDIRECT + 1];
};

// map major device number to device functions.
struct devsw
{
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
