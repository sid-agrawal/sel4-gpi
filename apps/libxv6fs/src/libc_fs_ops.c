/**
 * Implementations of libc fs ops using xv6fs functions
 */

#include <stdio.h>
#include <string.h>
#include <xv6fs/defs.h>
#include <xv6fs/fs.h>
#include <xv6fs/spinlock.h>
#include <xv6fs/sleeplock.h>
#include <xv6fs/file.h>
#include <xv6fs/stat.h>
#include <xv6fs/proc.h>

#define NO_OFFSET -1

__attribute__((noreturn))
void xv6fs_panic(char *s)
{
    printf("panic: %s\n", s);
    for (;;)
        ;
}

static struct proc curproc;

struct proc *myproc(void) {
  return &curproc;
}

#define FD_TABLE_SIZE 5120

void *fd_table[FD_TABLE_SIZE];

void fd_init(void)
{
    for (int i = 0; i < FD_TABLE_SIZE; i++)
    {
        fd_table[i] = NULL;
    }
}

long fd_bind(void *file)
{
    for (int i = 5; i < FD_TABLE_SIZE; i++)
        if (fd_table[i] == NULL)
        {
            fd_table[i] = file;
            return i;
        }
    return -1;
}

void *fd_get(long fd)
{
    if (fd >= 0 && fd < FD_TABLE_SIZE)
    {
        return fd_table[fd];
    }
    return NULL;
}

int xv6fs_open(const char *pathname, int flags, int modes)
{
    void *file = (void *)xv6fs_sys_open((char *)pathname, flags); // ARYA-TODO, what about modes?
    if (!file)
    {
        printf("%s in %s:xv6fs_sys_open failed,pathname(%s), flags(%d), modes(%d)\n", __func__, __FILE__, pathname, flags, modes);
        return -1;
    }
    long fd = fd_bind(file);
    return fd;
}

int xv6fs_read(int fd, void *buf, int count)
{
    void *file = fd_get(fd);
    if (!file)
    {
        printf("%s: fd_get failed\n", __func__);
        return -1;
    }
    return xv6fs_sys_read((char *)file, buf, count, NO_OFFSET);
}

int xv6fs_write(int fd, const void *buf, int count)
{
    void *file = fd_get(fd);
    if (!file)
    {
        printf("%s: fd_get failed\n", __func__);
        return -1;
    }
    long ret = xv6fs_sys_write((char *)file, (char *)buf, count, NO_OFFSET);
    return ret;
}

int xv6fs_fstat(int fd, struct stat *buf)
{
    void *file = fd_get(fd);
    if (!file)
    {
        printf("%s: fd_get failed\n", __func__);
        return -1;
    }
    uint64 t_mem[2]; // return ino & size
    xv6fs_sys_stat((char *)file, (uint64 *)t_mem);
    memset(buf, 0, sizeof(struct stat));
    buf->type = 0; // V_TYPE_FILE | V_IRWXU; // ARYA-TODO what is going on here
    buf->ino = t_mem[1];
    buf->size = t_mem[0];
    buf->dev = ROOTDEV;
    buf->nlink = 1;      // ARYA-TODO what is going on here

    return 0;
}

int xv6fs_stat(const char *pathname, struct stat *buf)
{

    void *file = (void *)xv6fs_sys_open((char *)pathname, O_CREATE | O_RDWR);
    if (!file)
    {
        printf("%s failed, pathname(%s)\n", __func__, pathname);
        return -1;
    }
    uint64 t_mem[2]; // return ino & size
    xv6fs_sys_stat((char *)file, (uint64 *)t_mem);
    memset(buf, 0, sizeof(struct stat));
    buf->type = 0; // V_TYPE_FILE | V_IRWXU; // ARYA-TODO what is going on here
    buf->ino = t_mem[1];
    buf->size = t_mem[0];
    buf->dev = ROOTDEV;
    buf->nlink = 1;      // ARYA-TODO what is going on here

    xv6fs_sys_fileclose(file);

    return 0;
}

int xv6fs_lseek(int fd, off_t offset, int whence)
{
    void *file = fd_get(fd);
    if (!file)
    {
        printf("%s: fd_get failed\n", __func__);
        return -1;
    }
    return xv6fs_sys_seek((char *)file, offset, whence);
}

int xv6fs_close(int fd)
{
    void *file = fd_get(fd);
    if (!file)
    {
        printf("%s fd_get failed\n", __func__);
        return -1;
    }
    return xv6fs_sys_fileclose((void *)file);
}

int xv6fs_unlink(const char *pathname)
{
    int r = xv6fs_sys_unlink((char *)pathname);
    return r;
}
