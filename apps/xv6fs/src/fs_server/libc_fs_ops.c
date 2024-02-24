/**
 * Implementations of libc fs ops using xv6fs functions
 */

#include <stdio.h>
#include <string.h>
#include <defs.h>
#include <fs.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <file.h>
#include <proc.h>

#define NO_OFFSET -1

__attribute__((noreturn)) void xv6fs_panic(char *s)
{
    printf("panic: %s\n", s);
    for (;;)
        ;
}

// ARYA-TODO track the actual process by client
static struct proc curproc;

struct proc *myproc(void)
{
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
            // printf("%s: new fd %d\n", __func__, i);
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
        // printf("%s: closed fd %d\n", __func__, fd);
    }
}

int xv6fs_open(const char *pathname, int flags, int modes)
{
    // printf("xv6fs_open: Opening file %s\n", pathname);
    void *file = (void *)xv6fs_sys_open((char *)pathname, flags); // ARYA-TODO, what about modes?
    if (!file)
    {
        // printf("%s: failed,pathname(%s), flags(%d), modes(%d)\n", __func__, pathname, flags, modes);
        return -1;
    }
    long fd = fd_bind(file);
    // printf("xv6fs_open: Opened file %s with fd %d\n", pathname, fd);
    return fd;
}

int xv6fs_read(int fd, void *buf, int count)
{
    // printf("Reading fd %d for %d bytes\n", fd, count);
    void *file = fd_get(fd);
    if (!file)
    {
        printf("%s: fd_get failed\n", __func__);
        return -1;
    }
    return xv6fs_sys_read((char *)file, buf, count, NO_OFFSET);
}

int xv6fs_pread(int fd, void *buf, int count, int offset)
{
    // printf("Reading fd %d at %d for %d bytes\n", fd, offset, count);
    void *file = fd_get(fd);
    if (!file)
    {
        printf("%s: fd_get failed\n", __func__);
        return -1;
    }
    int offset_before = ((struct file *)file)->off;
    int ret = xv6fs_sys_read((char *)file, buf, count, offset);
    ((struct file *)file)->off = offset_before;
    return ret;
}

int xv6fs_write(int fd, const void *buf, int count)
{
    // printf("Writing fd %d for %d bytes\n", count);
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
    // printf("%s: fd %d\n", __func__, fd);
    void *file = fd_get(fd);
    if (!file)
    {
        printf("%s: fd_get failed for fd %d\n", __func__, fd);
        return -1;
    }

    return xv6fs_sys_stat(file, (void *)buf);
}

int xv6fs_stat(const char *pathname, struct stat *buf)
{
    // printf("%s: %s\n", __func__, pathname);
    void *file = (void *)xv6fs_sys_open((char *)pathname, O_CREAT | O_RDWR);
    if (!file)
    {
        printf("%s failed, pathname(%s)\n", __func__, pathname);
        return -1;
    }
    int ret = xv6fs_sys_stat(file, (void *)buf);
    xv6fs_sys_fileclose(file);
    return ret;
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
    // printf("Closing fd %d\n", fd);
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

char *xv6fs_getcwd(char *buf, size_t size)
{
    char *r = xv6fs_sys_getcwd(buf, size);
    return r;
}

int xv6fs_fcntl(int fd, int cmd, unsigned long arg)
{
    void *file = fd_get(fd);
    if (!file)
    {
        printf("%s fd_get failed\n", __func__);
        return -1;
    }
    return xv6fs_sys_fcntl((void *)file, cmd, arg);
}