/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <autoconf.h>

#include <stdio.h>
#include <stdlib.h>
#include "hello.h"
#include <math.h>

#include <sel4bench/arch/sel4bench.h>
#include <sel4runtime.h>
/* dummy global for libsel4muslcsys */
char _cpio_archive[1];
char _cpio_archive_end[1];

#define APP_MALLOC_SIZE PAGE_SIZE_4K

char __attribute__((aligned(PAGE_SIZE_4K))) morecore_area[APP_MALLOC_SIZE];
size_t morecore_size = APP_MALLOC_SIZE;
/* Pointer to free space in the morecore area. */
uintptr_t morecore_top = (uintptr_t)&morecore_area[APP_MALLOC_SIZE];

int main(int argc, char **argv)
{
    int error;
    ccnt_t ctx_start, ctx_end;
    ccnt_t creation_start, creation_end;
    SEL4BENCH_READ_CCNT(creation_end);

    assert(argc > 0);
    seL4_CPtr ep = atol(argv[0]);

    printf("hello-native! argv[0] = %lx, creation end time: %ld\n", ep, creation_end);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, creation_end); // send a message saying we've started
    seL4_Send(ep, tag);

    return 0;
}
