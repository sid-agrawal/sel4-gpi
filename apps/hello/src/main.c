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
#include <sel4gpi/pd_utils.h>
#include <utils/ansi_color.h>

/* Initialization for static morecore */
#define APP_MALLOC_SIZE (PAGE_SIZE_4K)
char *morecore_area = (char *) PD_HEAP_LOC;
size_t morecore_size = APP_MALLOC_SIZE;
uintptr_t morecore_base = (uintptr_t) PD_HEAP_LOC;
uintptr_t morecore_top = (uintptr_t) (PD_HEAP_LOC + APP_MALLOC_SIZE);
extern __thread void *__sel4gpi_osm_data;

void calculateSD(float data[], float *mean, float *sd,
                 int start, int end);
int main(int argc, char **argv)
{
    sel4gpi_set_exit_cb();
    // sel4muslcsys_register_stdio_write_fn(write_buf);

    ccnt_t ctx_start, ctx_end;
    ccnt_t creation_start, creation_end;
    SEL4BENCH_READ_CCNT(creation_end);

    int error;

    ads_client_context_t ads_conn;
    ads_conn.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_space_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_VMR);
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();

    printf("Hello: ADS_CAP: %ld\n", (seL4_Word)ads_conn.badged_server_ep_cspath.capPtr);
    printf("Hello: PD_CAP: %ld\n", (seL4_Word)pd_conn.badged_server_ep_cspath.capPtr);
    // printf(COLORIZE("osm data: %p\n", CYAN), __sel4gpi_osm_data);

    seL4_CPtr mo_server_ep = sel4gpi_get_rde(GPICAP_TYPE_MO);
    assert(mo_server_ep != seL4_CapNull);

    seL4_CPtr slot;
    printf("HELLO: getting next slot\n");

    mo_client_context_t mo_conn;
    error = mo_component_client_connect(mo_server_ep,
                                        5,
                                        MO_PAGE_BITS,
                                        &mo_conn);

    assert(error == 0);

    void *ret_vaddr;
    error = ads_client_attach(&ads_conn,
                              0, /*vaddr*/
                              &mo_conn,
                              SEL4UTILS_RES_TYPE_GENERIC,
                              &ret_vaddr);
    assert(error == 0);
    printf("Attached to vaddr %p\n", ret_vaddr);

    error = pd_client_next_slot(&pd_conn, &slot);
    assert(error == 0);
    printf("Next free slot is %ld\n", (seL4_Word)slot);

    if (argc > 0)
    {
        seL4_CPtr cap_arg = (seL4_CPtr)strtol(argv[0], NULL, 10);
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
        printf("argc: %d cap_arg: %lx\n", argc, cap_arg);

        ep_client_context_t parent_ep = {.badged_server_ep_cspath.capPtr = cap_arg};
        error = ep_client_get_raw_endpoint(&parent_ep);
        assert(error == 0 && parent_ep.raw_endpoint != seL4_CapNull);

        seL4_Call(parent_ep.raw_endpoint, tag);

        seL4_Word slot = seL4_GetMR(0);

        mo_conn.badged_server_ep_cspath.capPtr = (seL4_CPtr)slot;
        error = ads_client_attach(&ads_conn,
                                  0,
                                  &mo_conn,
                                  SEL4UTILS_RES_TYPE_GENERIC,
                                  &ret_vaddr);
        assert(error == 0);
        printf("Attached given MO to vaddr %p\n", ret_vaddr);

        // tell sender we're done
        seL4_Send(parent_ep.raw_endpoint, tag);
    }

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