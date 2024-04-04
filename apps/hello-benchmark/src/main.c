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
    bool native = (bool)atol(argv[1]);

    printf("hello_benchmark %s! argv[0] = %lx, creation end time: %ld\n",
           get_bench_type_name(native), ep, creation_end);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, BM_PD_CREATE); // send a message saying we've started
    seL4_SetMR(1, creation_end);
    seL4_Send(ep, tag);

    tag = seL4_MessageInfo_new(0, 0, 0, 2);
    ccnt_t ipc_round_start;
    ccnt_t ipc_round_end;
    SEL4BENCH_READ_CCNT(ipc_round_start);
    printf("%s: IPC benchmark: start %ld\n", get_bench_type_name(native), ipc_round_start);
    seL4_SetMR(0, BM_IPC);
    seL4_SetMR(1, ipc_round_start);
    printf("hello_benchmark! seL4Call\n");
    seL4_Call(ep, tag);

    seL4_Word bench_type = seL4_GetMR(0);
    assert(bench_type == BM_IPC);
    SEL4BENCH_READ_CCNT(ipc_round_end);
    printf("%s: IPC roundtrip end: %ld, round trip total: %ld\n", get_bench_type_name(native), ipc_round_end, ipc_round_end - ipc_round_start);

    printf("%s: Goodbye cruel world!\n", get_bench_type_name(native));
    return 0;
}
