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
#include <xv6fs/proc.h>

#define NO_OFFSET -1

__attribute__((noreturn))
void xv6fs_panic(char *s)
{
    printf("panic: %s\n", s);
    for (;;)
        ;
}

// ARYA-TODO track the actual process by client
static struct proc curproc;

struct proc *myproc(void) {
  return &curproc;
}

// ARYA-TODO set up a better fd table
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

void fd_close(long fd)
{
    if (fd >= 0 && fd < FD_TABLE_SIZE)
    {
        fd_table[fd] = NULL;
    }
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

    return xv6fs_sys_stat(file , (void *)buf);
}

int xv6fs_stat(const char *pathname, struct stat *buf)
{

    void *file = (void *)xv6fs_sys_open((char *)pathname, O_CREAT | O_RDWR);
    if (!file)
    {
        printf("%s failed, pathname(%s)\n", __func__, pathname);
        return -1;
    }

    return xv6fs_sys_stat(file , (void *)buf);
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
    fd_close(fd);
    return xv6fs_sys_fileclose((void *)file);
}

int xv6fs_unlink(const char *pathname)
{
    int r = xv6fs_sys_unlink((char *)pathname);
    return r;
}
