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

#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>

#define APP_MALLOC_SIZE PAGE_SIZE_4K

char __attribute__((aligned(PAGE_SIZE_4K))) morecore_area[APP_MALLOC_SIZE];
size_t morecore_size = APP_MALLOC_SIZE;
/* Pointer to free space in the morecore area. */
uintptr_t morecore_top = (uintptr_t)&morecore_area[APP_MALLOC_SIZE];

void calculateSD(float data[], float *mean, float *sd,
                 int start, int end);
int main(int argc, char **argv)
{
    // sel4muslcsys_register_stdio_write_fn(write_buf);

    ccnt_t ctx_start, ctx_end;
    ccnt_t creation_start, creation_end;
    SEL4BENCH_READ_CCNT(creation_end);

    // Do we need to initialize a vka?
    // No we can add a function called, next PD slot.

    seL4_CPtr ads_cap = sel4runtime_get_initial_ads_cap();
    seL4_CPtr rde_cap = sel4runtime_get_rde_cap();

    printf("Hello: ADS_CAP: %ld\n", (seL4_Word)ads_cap);
    printf("Hello: RDE_CAP: %ld\n", (seL4_Word)rde_cap);

    ads_client_context_t ads_conn;
    ads_conn.badged_server_ep_cspath.capPtr = ads_cap;

    void *rde_vaddr;
    mo_client_context_t rde_mo;
    rde_mo.badged_server_ep_cspath.capPtr = rde_cap;
    int error = ads_client_attach(&ads_conn,
                              0, /*vaddr*/
                              &rde_mo,
                              &rde_vaddr);
    assert(error == 0);
    printf("Attached to vaddr %p\n", rde_vaddr);

    osmosis_rde_t *pd_rde = (osmosis_rde_t *) rde_vaddr;
    seL4_CPtr gpi_cap = pd_rde[GPICAP_TYPE_MO].slot_in_PD;

    seL4_CPtr slot;
    pd_client_context_t pd_conn;
    printf("pd_rde[pd]: %lx\n", pd_rde[GPICAP_TYPE_PD].slot_in_PD);
    pd_conn.badged_server_ep_cspath.capPtr = pd_rde[GPICAP_TYPE_PD].slot_in_PD;
    error = pd_client_next_slot(&pd_conn, &slot);
    assert(error == 0);
    printf("Next free slot is %ld\n", (seL4_Word)slot);

    mo_client_context_t mo_conn;
    error = mo_component_client_connect(gpi_cap,
                                        slot,
                                        5,
                                        &mo_conn);

    assert(error == 0);

    void *ret_vaddr;
    error = ads_client_attach(&ads_conn,
                              0, /*vaddr*/
                              &mo_conn,
                              &ret_vaddr);
    assert(error == 0);
    printf("Attached to vaddr %p\n", ret_vaddr);

    error = pd_client_next_slot(&pd_conn, &slot);
    assert(error == 0);
    printf("Next free slot is %ld\n", (seL4_Word)slot);

    printf(".... Goodbye Cruel World\n");
    return 0;
#if 0
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
#endif
}

void calculateSD(float data[], float *mean, float *sd,
                 int start, int end)
{
    int i;

    int n = end - start + 1;
    float sum = 0.0;
    for (i = 1; i < n; ++i)
    {
        sum += data[i];
    }
    *mean = sum / n;
    for (i = start; i < end; ++i)
    {
        *sd += pow(data[i] - *mean, 2);
    }
    *sd = *sd / n;
    *sd = sqrt(*sd);
    return;
}