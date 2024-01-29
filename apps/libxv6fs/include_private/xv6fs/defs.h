#pragma once

#include <stdio.h>
#include <xv6fs/types.h>
#include <xv6fs/param.h>
#include <xv6fs/fcntl.h>

struct buf;
struct context;
struct dirent;
struct file;
struct inode;
struct pipe;
struct proc;
struct spinlock;
struct sleeplock;
struct stat;
struct superblock;

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

// ramdisk.c
void ramdiskinit(void);
void ramdiskintr(void);
void ramdiskrw(struct buf *);

// kalloc.c
void *kalloc(void);
void kfree(void *);
void kinit(void);

// log.c
void initlog(int, struct superblock *);
void log_write(struct buf *);
void begin_op(void);
void end_op(void);

// pipe.c
int pipealloc(struct file **, struct file **);
void pipeclose(struct pipe *, int);
int piperead(struct pipe *, uint64, int);
int pipewrite(struct pipe *, uint64, int);

// printf.c
__attribute__((noreturn))
void xv6fs_panic(char *s);

// proc.c
int cpuid(void);
void exit(int);
int fork(void);
int growproc(int);
int kill(int);
int killed(struct proc *);
void setkilled(struct proc *);
struct cpu *mycpu(void);
struct cpu *getmycpu(void);
struct proc *myproc();
void procinit(void);
void scheduler(void) __attribute__((noreturn));
void sched(void);
void xv6fs_sleep(void *, struct spinlock *);
void userinit(void);
int wait(uint64);
void wakeup(void *);
void yield(void);
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int either_copyin(void *dst, int user_src, uint64 src, uint64 len);
void procdump(void);

// spinlock.c
void acquire(struct spinlock *);
int holding(struct spinlock *);
void initlock(struct spinlock *, char *);
void release(struct spinlock *);
void push_off(void);
void pop_off(void);

// sleeplock.c
void acquiresleep(struct sleeplock *);
void releasesleep(struct sleeplock *);
int holdingsleep(struct sleeplock *);
void initsleeplock(struct sleeplock *, char *);

// string.c
/*
int             memcmp(const void*, const void*, uint);
void*           memmove(void*, const void*, uint);
void*           memset(void*, int, uint);
char*           safestrcpy(char*, const char*, int);
int             strlen(const char*);
int             strncmp(const char*, const char*, uint);
char*           strncpy(char*, const char*, int);
*/

// functions for port to osmosis
void xv6fs_bread(uint sec, void *buf);
void xv6fs_bwrite(uint sec, void *buf);
void disk_rw(struct buf *, int);
int init_disk_file(void);
void fd_init(void);

// syscall functions
struct file *xv6fs_sys_open(char *path, int omode);
int xv6fs_sys_fileclose(void *fh);
int xv6fs_sys_read(void *fh, char *buf, size_t sz, uint off);
int xv6fs_sys_write(void *fh, char *buf, size_t sz, uint off);
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
int xv6fs_sys_stat(void *fh, uint64 *t_mem);
int xv6fs_sys_seek(void *fh, uint64 off, int whence);

// libc functions
int xv6fs_open(const char *pathname, int flags, int modes);
int xv6fs_read(int fd, void *buf, int count);
int xv6fs_write(int fd, const void *buf, int count);
int xv6fs_fstat(int fd, struct stat *buf);
int xv6fs_stat(const char *pathname, struct stat *buf);
int xv6fs_lseek(int fd, off_t offset, int whence);
int xv6fs_close(int fd);
int xv6fs_unlink(const char *pathname);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x) / sizeof((x)[0]))
