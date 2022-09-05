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


    printf("Hello: arg0: %s\n", argv[0]);
    ccnt_t end;
    SEL4BENCH_READ_CCNT(end);

    /*
     * send a message to our parent, and wait for a reply
     */

    /* set the data to send. We send it in the first message register */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, end);

    
    ZF_LOGF_IF(argc < 1,
               "Missing arguments.\n");
    seL4_CPtr ep = (seL4_CPtr) atol(argv[0]);
    tag = seL4_Call(ep, tag);

    seL4_Word msg = seL4_GetMR(0);

    // printf("hello: got a reply: %lu\n", msg);

    return 0;
}
