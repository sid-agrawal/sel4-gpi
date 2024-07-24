#pragma once

#include <stdint.h>

struct file
{
  enum
  {
    FD_NONE,
    FD_PIPE,
    FD_INODE,
    FD_DEVICE
  } type;
  uint32_t id; // unique ID of the file
  int ref;     // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint32_t off;      // FD_INODE
  short major;       // FD_DEVICE
  uint64_t flags;
};

// in-memory copy of an inode
struct inode
{
  // ARYA-TODO we have no mode / permissions / ownership, does that matter?

  uint32_t dev;          // Device number
  uint32_t inum;         // Inode number
  int ref;               // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;             // inode has been read from disk?
  short type;
  short major;
  short minor;
  short nlink;
  uint32_t size;
  int ctime;
  int atime;
  int mtime;
  uint32_t addrs[NDIRECT + 1];
};

// map major device number to device functions.
struct devsw
{
  int (*read)(int, uint64_t, int);
  int (*write)(int, uint64_t, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
