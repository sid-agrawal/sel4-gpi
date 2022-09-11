/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <autoconf.h>

#include <stdio.h>
#include <stdlib.h>
#include "hello.h"

#include <sel4bench/arch/sel4bench.h>

int main(int argc, char **argv)
{
//    sel4muslcsys_register_stdio_write_fn(write_buf);

    ccnt_t ctx_start, ctx_end;
    ccnt_t creation_start, creation_end;
    SEL4BENCH_READ_CCNT(creation_end);

    printf("Hello: arg0: %s\n", argv[0]);

    /*
     * send a message to our parent, and wait for a reply
     */

    /* set the data to send. We send it in the first message register */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_CPtr ep = (seL4_CPtr) atol(argv[0]);

    SEL4BENCH_READ_CCNT(ctx_start);
    tag = seL4_Call(ep, tag);
    SEL4BENCH_READ_CCNT(ctx_end);

    creation_start = seL4_GetMR(0);

    printf("hello: Creationg Time : %lu cycles\n", creation_end - creation_start);
    printf("hello: Cross AS IPC RTT: %lu cycles\n", ctx_end - ctx_start);

    return 0;
}
