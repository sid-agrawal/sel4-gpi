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
#include <sel4test/test.h>
#include <sel4gpi/bench_utils.h>
#include <sel4gpi/pd_utils.h>

/* dummy global for libsel4muslcsys */
char _cpio_archive[1];
char _cpio_archive_end[1];

/* Initialization for static morecore */
#define APP_MALLOC_SIZE (PAGE_SIZE_4K)
char *morecore_area = (char *)PD_HEAP_LOC;
size_t morecore_size = APP_MALLOC_SIZE;
uintptr_t morecore_base = (uintptr_t)PD_HEAP_LOC;
uintptr_t morecore_top = (uintptr_t)(PD_HEAP_LOC + APP_MALLOC_SIZE);

int main(int argc, char **argv)
{
    int error;
    ccnt_t ctx_start, ctx_end;
    ccnt_t creation_start, creation_end;

    // Record creation end time
    SEL4BENCH_READ_CCNT(creation_end);

    // Get args
    assert(argc > 0);
    seL4_CPtr ep = atol(argv[0]);
    bool native = (bool)atol(argv[1]);

    printf("hello_benchmark main! creation end time: %ld\n", creation_end);

    // Send a message to parent with creation end time
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, BM_PD_CREATE);
    seL4_SetMR(1, creation_end);
    seL4_Send(ep, tag);

    // Wait for message from parent to time IPC
    seL4_Word sender;
    tag = seL4_Recv(ep, &sender);
    tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, BM_IPC);
    seL4_Reply(tag);

    // Block this PD so it does not exit
    seL4_Recv(ep, &sender);

    printf("ERROR: hello_benchmark should not have gotten here");

    return 0;
}
