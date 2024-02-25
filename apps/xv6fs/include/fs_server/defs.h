#pragma once

#define __NEED_time_t
#define __NEED_struct_timespec

#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <fs_shared.h>
#include <types.h>
#include <fs.h>
#include <spinlock.h>
#include <sleeplock.h>

struct buf;
struct dirent;
struct file;
struct inode;
struct pipe;
struct proc;
struct spinlock;
struct sleeplock;
struct stat;
struct superblock;

// file types
#define T_DIR 1    // Directory
#define T_FILE 2   // File
#define T_DEVICE 3 // Device
#define T_SOCK 4   // Unix-domain socket

// bio.c
void binit(void);
struct buf *bread(uint, uint);
void brelse(struct buf *);
void bwrite(struct buf *);
void bpin(struct buf *);
void bunpin(struct buf *);

// file.c
struct file *filealloc(void);
void fileclose(struct file *);
struct file *filedup(struct file *);
void fileinit(void);
int fileread(struct file *, uint64, int n);
int filestat(struct file *, uint64 addr);
int filewrite(struct file *, uint64, int n);

// fs.c
void fsinit(int);
void readsb(int dev, struct superblock *sb);
int dirlink(struct inode *, char *, uint);
struct inode *dirlookup(struct inode *, char *, uint *);
struct inode *ialloc(uint, short);
struct inode *idup(struct inode *);
void iinit();
void ilock(struct inode *);
void iput(struct inode *);
void iunlock(struct inode *);
void iunlockput(struct inode *);
void iupdate(struct inode *);
int namecmp(const char *, const char *);
struct inode *namei(char *);
struct inode *nameiparent(char *, char *);
int readi(struct inode *, int, uint64, uint, uint);
void stati(struct inode *, struct stat *);
int writei(struct inode *, int, uint64, uint, uint);
void itrunc(struct inode *);
static void xv6fs_bzero(int dev, int bno);

// log.c
void initlog(int, struct superblock *);
void log_write(struct buf *);
void begin_op(void);
void end_op(void);

// printf.c
__attribute__((noreturn)) void xv6fs_panic(char *s);

// proc.c
struct proc *myproc();
void xv6fs_sleep(void *, struct spinlock *);
void wakeup(void *);

// spinlock.c
void acquire(struct spinlock *);
int holding(struct spinlock *);
void initlock(struct spinlock *, char *);
void release(struct spinlock *);

// sleeplock.c
void acquiresleep(struct sleeplock *);
void releasesleep(struct sleeplock *);
int holdingsleep(struct sleeplock *);
void initsleeplock(struct sleeplock *, char *);

// functions for port to osmosis
void xv6fs_bread(uint sec, void *buf);
void xv6fs_bwrite(uint sec, void *buf);
void disk_rw(struct buf *, int);
int init_disk_file(void);
void fd_init(void);

// syscall functions
struct file *xv6fs_sys_open(char *path, int omode);
int xv6fs_sys_fileclose(void *fh);
int xv6fs_sys_read(struct file *f, char *buf, size_t sz, uint off);
int xv6fs_sys_write(struct file *f, char *buf, size_t sz, uint off);
int xv6fs_sys_fstat(char *path, void *buf);
int xv6fs_sys_readdirent(void *fh, struct dirent *e, uint off);
int xv6fs_sys_truncate(char *path);
int xv6fs_sys_mkdir(char *path);
int xv6fs_sys_mksock(char *path);
int xv6fs_sys_dounlink(char *path);
int xv6fs_sys_unlink(char *path);
int xv6fs_sys_dolink(char *old, char *new);
int xv6fs_sys_rename(char *path1, char *path2);
int xv6fs_sys_utime(char *path, int time);
int xv6fs_sys_stat(void *fh, void *sth);
int xv6fs_sys_seek(void *fh, uint64 off, int whence);
char *xv6fs_sys_getcwd(char *buf, size_t size);
int xv6fs_sys_fcntl(void *fh, int cmd, unsigned long arg);

// libc functions
int xv6fs_open(const char *pathname, int flags, int modes);
int xv6fs_read(int fd, void *buf, int count);
int xv6fs_pread(int fd, void *buf, int count, int offset);
int xv6fs_write(int fd, const void *buf, int count);
int xv6fs_fstat(int fd, struct stat *buf);
int xv6fs_stat(const char *pathname, struct stat *buf);
int xv6fs_lseek(int fd, off_t offset, int whence);
int xv6fs_close(int fd);
int xv6fs_unlink(const char *pathname);
char *xv6fs_getcwd(char *buf, size_t size);
int xv6fs_fcntl(int fd, int cmd, unsigned long arg);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x) / sizeof((x)[0]))
