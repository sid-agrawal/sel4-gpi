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

void calculateSD(float data[], float *mean, float *sd,
                 int start, int end);
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

    printf("hello: Creationg Time : %lu cycles\n", (creation_end - creation_start)/1000);

    float data[1000];
    printf("test_func_die: Cross AS IPC Time\n");
    int count = 1000;
    int i = 0;
    for(int i = 0; i < count; i++) {

        tag = seL4_MessageInfo_new(0, 0, 0, 1);
        SEL4BENCH_READ_CCNT(ctx_start);
        tag = seL4_Call(ep, tag);
        SEL4BENCH_READ_CCNT(ctx_end);
        data[i] = (ctx_end - ctx_start)/2;
    }
    
    float mean, sd;
    calculateSD(data, &mean, &sd, 1, 99);
    printf("MEAN: %f, SD: %f \n", mean/1000, sd/1000);
    return 0;
}

void calculateSD(float data[], float *mean, float *sd,
                 int start, int end)
{
    int i;

    int n = end - start +1;
    float sum = 0.0;
    for (i = 1; i < n; ++i) {
        sum += data[i];
    }
    *mean = sum / n;
    for (i = start; i < end; ++i) {
        *sd += pow(data[i] - *mean, 2);
    }
    *sd = *sd / n;
    *sd = sqrt(*sd);
    return;
}