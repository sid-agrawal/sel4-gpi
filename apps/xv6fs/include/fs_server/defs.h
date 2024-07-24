#pragma once

#define __NEED_time_t
#define __NEED_struct_timespec

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sel4/types.h>

#include <fs_shared.h>
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

// other constants
#define NO_OFFSET -1

// bio.c
void binit(void);
struct buf *bread(uint32_t, uint32_t);
void brelse(struct buf *);
void bwrite(struct buf *);
void bpin(struct buf *);
void bunpin(struct buf *);

// file.c
struct file *filealloc(void);
void fileclose(struct file *);
struct file *filedup(struct file *);
int fileread(struct file *, uint64_t, int n);
int filestat(struct file *, uint64_t addr);
int filewrite(struct file *, uint64_t, int n);
int file_blocknos(struct file *f, int *buf, int buf_size, int *result_size);

// fs.c
void fsinit(int);
void readsb(int dev, struct superblock *sb);
int dirlink(struct inode *, char *, uint32_t);
struct inode *dirlookup(struct inode *, char *, uint32_t *);
struct inode *ialloc(uint32_t, short);
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
uint32_t bmap_noalloc(struct inode *ip, uint32_t bn);
int readi(struct inode *, int, uint64_t, uint32_t, uint32_t);
void stati(struct inode *, struct stat *);
int writei(struct inode *, int, uint64_t, uint32_t, uint32_t);
void itrunc(struct inode *);
static void xv6fs_bzero(int dev, int bno);
int iblocknos(uint32_t dev, uint32_t inum, int *buf, int buf_size, int *result_size);

// printf.c
__attribute__((noreturn)) void xv6fs_panic(char *s);

// proc.c
struct proc *myproc();

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

/**
 * The file system uses this to request to read one of the file system's blocks
 * The FS server needs to implement this function
 * 
 * @param sec the block number according to the file system - a logical block number
 * @param buf the buffer to read the block into
 */
void xv6fs_bread(uint32_t sec, void *buf);

/**
 * The file system uses this to request to write one of the file system's blocks
 * The FS server needs to implement this function
 * 
 * @param sec the block number according to the file system - a logical block number
 * @param buf the buffer containing information to write
 */
void xv6fs_bwrite(uint32_t sec, void *buf);
void disk_rw(struct buf *, int);

/**
 * The file system uses this to notify the file server when a block is assigned to a file
 * The FS server needs to implement this function
 * 
 * @param file_id the file's inum
 * @param blockno the block being assigned to it
 */
void map_file_to_block(uint32_t file_id, uint32_t blockno);

/// (XXX) ??
int init_disk_file(void);

// syscall functions
// (XXX) Arya: These need to be documented
struct file *xv6fs_sys_open(char *path, int omode);
int xv6fs_sys_fileclose(void *fh);
int xv6fs_sys_read(struct file *f, char *buf, size_t sz, uint32_t off);
int xv6fs_sys_write(struct file *f, char *buf, size_t sz, uint32_t off);
int xv6fs_sys_fstat(char *path, void *buf);
int xv6fs_sys_readdirent(void *fh, struct dirent *e, uint32_t off);
int xv6fs_sys_truncate(char *path);
int xv6fs_sys_mkdir(char *path);
int xv6fs_sys_rmdir(char *path, bool delete_contents);
int xv6fs_sys_mksock(char *path);

/**
 * Unlink the path from its currently linked inode
 * 
 * @param path the path to unlink
 * @param inum returns the inum of the inode that the path previously linked to (optional)
 * @param was_last_link returns true if this was the last link to the inode (optional)
 *                      (in this case, the inode will be deleted)
 */
int xv6fs_sys_unlink(char *path, uint32_t *inum, bool *was_last_link);
int xv6fs_sys_dolink(char *old, char *new);
int xv6fs_sys_dolink2(struct file *old, char *new);
int xv6fs_sys_rename(char *path1, char *path2);
int xv6fs_sys_utime(char *path, int time);
int xv6fs_sys_stat(struct file *f, struct stat *st);
int xv6fs_sys_seek(void *fh, uint64_t off, int whence);
int xv6fs_sys_fcntl(void *fh, int cmd, unsigned long arg);
int xv6fs_sys_blocknos(struct file *f, int *buf, int buf_size, int* result_size);
int xv6fs_sys_inode_blocknos(int inum, int *buf, int buf_size, int *result_size);
int xv6fs_sys_walk(char *path, bool print, uint32_t *inums, int *n_files);
struct inode *dirlookup_idx(struct inode *dp, int idx, char *de_name);
