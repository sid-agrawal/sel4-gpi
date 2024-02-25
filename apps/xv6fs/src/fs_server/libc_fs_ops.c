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