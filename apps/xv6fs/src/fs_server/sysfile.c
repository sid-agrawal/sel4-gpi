//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include <stdio.h>
#include <string.h>
#include <defs.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <proc.h>
#include <fs.h>
#include <file.h>

#define NO_OFFSET -1

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for (off = 2 * sizeof(de); off < dp->size; off += sizeof(de))
  {
    if (readi(dp, 0, (uint64_t)&de, off, sizeof(de)) != sizeof(de))
      xv6fs_panic("isdirempty: readi");
    if (de.inum != 0)
      return 0;
  }
  return 1;
}

static struct inode *
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if ((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if ((ip = dirlookup(dp, name, 0)) != 0)
  {
    iunlockput(dp);
    ilock(ip);
    if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if ((ip = ialloc(dp->dev, type)) == 0)
  {
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if (type == T_DIR)
  { // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if (dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if (type == T_DIR)
  {
    // now that success is guaranteed:
    dp->nlink++; // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

/* xv6 "system call" functions */

uint32_t xv6fs_file_ino(void *file)
{
  struct file *f = (struct file *)file;
  return (f->ip)->inum;
}

struct file *
xv6fs_sys_open(char *path, int omode)
{
  // printf("%s: for path %s\n", __func__, path);

  struct file *f;
  struct inode *ip;

  if (omode & O_CREAT)
  {
    ip = create(path, T_FILE, 0, 0);
    if (ip == 0)
    {
      return 0;
    }
  }
  else
  {
    if ((ip = namei(path)) == 0)
    {
      return 0;
    }
    ilock(ip);
    if (ip->type == T_DIR && omode != O_RDONLY)
    {
      iunlockput(ip);
      return 0;
    }
  }

  if ((f = filealloc()) == 0)
  {
    fprintf(stderr, "%s filealloc error!\n", __func__);
    if (f)
      fileclose(f);
    iunlockput(ip);
    return 0;
  }
  iunlock(ip);

  f->type = FD_INODE;
  f->ip = ip;
  f->id = ip->inum;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  f->flags = omode;
  return f;
}

int xv6fs_sys_fileclose(void *fh)
{
  struct file *f = (struct file *)fh;
  fileclose(f);
  return 0;
}

int xv6fs_sys_read(struct file *f, char *buf, size_t sz, uint32_t off)
{
  if (f == 0)
    return -1;
  if (off != NO_OFFSET)
    f->off = off;

  return fileread(f, (uint64_t)buf, sz);
}

// Writes all blocknos for file f in the buf
// If the buf runs out of size, stops writing and returns -1
int xv6fs_sys_blocknos(struct file *f, int *buf, int buf_size, int* result_size)
{ 
  if (f == 0)
    return -1;

  return file_blocknos(f, buf, buf_size, result_size);
}

int xv6fs_sys_write(struct file *f, char *buf, size_t sz, uint32_t off)
{
  if (f == 0)
    return -1;
  if (off != NO_OFFSET)
    f->off = off;

  int r = filewrite(f, (uint64_t)buf, sz);
  return r;
}

int xv6fs_sys_fstat(char *path, void *buf)
{
  // printf("xv6fs_sys_fstat opening %s\n", path);
  struct file *f = xv6fs_sys_open(path, O_RDONLY);
  if (f == 0)
    return -1;
  int r = filestat(f, (uint64_t)buf);
  fileclose(f);
  return r;
}

int xv6fs_sys_readdirent(void *fh, struct dirent *e, uint32_t off)
{
  struct file *f = (struct file *)fh;
  if ((f == 0) || (f->ip->type != T_DIR))
    return -1;
  if (off >= f->ip->size)
    return 0;
  f->off = off;
  int r = fileread(f, (uint64_t)e, sizeof(*e));
  return r;
}

int xv6fs_sys_truncate(char *path)
{
  // printf("xv6fs_sys_truncate opening %s\n", path);
  struct file *f = xv6fs_sys_open(path, O_RDWR);
  if (f == 0)
    return -1;
  itrunc(f->ip);
  fileclose(f);
  return 0;
}

int xv6fs_sys_mkdir(char *path)
{
  struct inode *ip;

  if ((ip = create(path, T_DIR, 0, 0)) == 0)
  {
    return -1;
  }
  iunlockput(ip);
  return 0;
}

int xv6fs_sys_mksock(char *path)
{
  struct inode *ip;

  if ((ip = create(path, T_SOCK, 0, 0)) == 0)
  {
    return -1;
  }
  iunlockput(ip);
  return 0;
}

int xv6fs_sys_unlink(char *path)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ];
  uint32_t off;
  int r = -1;

  if ((dp = nameiparent(path, name)) == 0)
  {
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if ((ip = dirlookup(dp, name, &off)) == 0)
  {
    r = ENOENT;
    goto bad;
  }

  ilock(ip);

  if (ip->nlink < 1)
    xv6fs_panic("unlink: nlink < 1");

  if (ip->type == T_DIR && !isdirempty(ip))
  {
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if (writei(dp, 1, (uint64_t)&de, off, sizeof(de)) != sizeof(de))
    xv6fs_panic("unlink: writei");
  if (ip->type == T_DIR)
  {
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  // fprintf(stderr,"%s name:%s, updated ip->nlink:%d\n",__func__, path, ip->nlink);
  iupdate(ip);
  iunlockput(ip);

  return 0;

bad:
  iunlockput(dp);
  return r;
}

// Create the path new as a link to the same inode as old.
int xv6fs_sys_dolink(char *old, char *new)
{
  struct inode *dp, *ip;
  char name[DIRSIZ];

  if ((ip = namei(old)) == 0)
  {
    return -1;
  }

  ilock(ip);
  if (ip->type == T_DIR)
  {
    iunlockput(ip);
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if ((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0)
  {
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  return -1;
}

int xv6fs_sys_rename(char *path1, char *path2)
{
  int r = -1;

  r = xv6fs_sys_unlink(path2);
  if ((r == 0) || (r == ENOENT))
  {
    r = xv6fs_sys_dolink(path1, path2);
    if (r == 0)
    {
      r = xv6fs_sys_unlink(path1);
    }
  }
  return r;
}

int xv6fs_sys_utime(char *path, int time)
{
  // printf("xv6fs_sys_utime opening %s\n", path);
  struct file *f = xv6fs_sys_open(path, O_WRONLY);
  if (f == 0)
    return -1;
  ilock(f->ip);
  f->ip->mtime = time;
  iupdate(f->ip);
  iunlock(f->ip);
  fileclose(f);
  return 0;
}

int xv6fs_sys_stat(struct file *f, struct stat *st)
{
  ilock(f->ip);
  stati(f->ip, st);
  iunlock(f->ip);
  return 0;
}

int xv6fs_sys_seek(void *fh, uint64_t off, int whence)
{
  struct file *f = (struct file *)fh;
  if (f == 0)
    return -1;

  switch (whence)
  {
  case SEEK_SET:
    f->off = off;
    break;
  case SEEK_CUR:
    f->off += off;
    break;
  case SEEK_END:
    f->off = f->ip->size + off;
  }

  return f->off;
}

// ARYA-TODO support real locks?
int xv6fs_sys_fcntl(void *fh, int cmd, unsigned long arg)
{
  struct file *f = (struct file *)fh;

  int res = 0;

  switch (cmd)
  {
  case F_SETFL:
    uint64_t flags_mask = O_APPEND | O_ASYNC | O_NONBLOCK;
    f->flags = (f->flags & ~flags_mask) | (arg & flags_mask);
    // printf("xv6fs_sys_fcntl: F_SETFL\n");
    break;
  case F_GETFL:
    res = f->flags;
    // printf("xv6fs_sys_fcntl: F_GETFL\n");
    break;
  case F_GETOWN:
    printf("xv6fs_sys_fcntl: Unsupported cmd F_GETOWN\n");
    break;
  case F_DUPFD_CLOEXEC:
    printf("xv6fs_sys_fcntl: Unsupported cmd F_DUPFD_CLOEXEC\n");
    break;
  case F_SETLK:
    // printf("xv6fs_sys_fcntl: F_SETLK\n");
    break;
  case F_SETLKW:
    // printf("xv6fs_sys_fcntl: F_SETLKW\n");
    break;
  case F_GETLK:
    struct flock *lk = (struct flock *)arg;
    lk->l_type = F_UNLCK;
    // printf("xv6fs_sys_fcntl: F_GETLK\n");
    break;
  case F_GETOWN_EX:
    printf("xv6fs_sys_fcntl: Unsupported cmd F_GETOWN_EX\n");
    break;
  case F_SETOWN_EX:
    printf("xv6fs_sys_fcntl: Unsupported cmd F_SETOWN_EX\n");
    break;
  default:
    printf("xv6fs_sys_fcntl: Unknown cmd %d\n", cmd);
    break;
  }

  return 0;
}