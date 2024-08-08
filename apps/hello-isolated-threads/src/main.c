/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <autoconf.h>

#include <stdio.h>
#include <stdlib.h>
#include <sel4runtime.h>
/* dummy global for libsel4muslcsys */
char _cpio_archive[1];
char _cpio_archive_end[1];

#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/vmr_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/error_handle.h>

/* Initialization for static morecore */
#define APP_MALLOC_SIZE (PAGE_SIZE_4K)
char *morecore_area = (char *)PD_HEAP_LOC;
size_t morecore_size = APP_MALLOC_SIZE;
uintptr_t morecore_base = (uintptr_t)PD_HEAP_LOC;
uintptr_t morecore_top = (uintptr_t)(PD_HEAP_LOC + APP_MALLOC_SIZE);
extern __thread void *__sel4gpi_osm_data;

#define GARBAGE_DATA 0xDEADBEEF

int isolated_thread(int argc, char **argv)
{
    int error = 0;
    ep_client_context_t fault_ep_conn = sel4gpi_get_fault_ep_conn();
    error = ep_client_get_raw_endpoint(&fault_ep_conn);
    printf("in thread with isolated stack!\n");

    assert(argc >= 1);
    uint64_t *main_thread_frame_addr = (uint64_t *)atol(argv[0]);
    printf("got main thread's frame address (%p), attempting to garble\n", main_thread_frame_addr);
    *main_thread_frame_addr = GARBAGE_DATA; // attempt to garble

    /* we may not cause a fault if the main thread's stack addr is also mapped to something in our ADS,
     * so, notify parent of completion
     */
    seL4_MessageInfo_t msg = seL4_MessageInfo_new(0, 0, 0, 0);
    seL4_Send(fault_ep_conn.raw_endpoint, msg);
    printf("exiting %s\n", __func__);
    return error;
}

int main(int argc, char **argv)
{
    int error;

    ads_client_context_t ads_conn = sel4gpi_get_ads_conn();
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();
    ep_client_context_t fault_ep_conn = sel4gpi_get_fault_ep_conn();
    error = ep_client_get_raw_endpoint(&fault_ep_conn);
    GOTO_IF_ERR(error, "Failed to get raw fault handler EP\n");

    printf("Hello: ADS_CAP: %lu PD_CAP: %lu FAULT_EP_CAP: %lu\n", ads_conn.ep, pd_conn.ep, fault_ep_conn.ep);

    sel4gpi_runnable_t runnable = {0};
    pd_config_t *cfg = sel4gpi_new_runnable(true, true, &runnable);
    test_assert(cfg != NULL);

    /* isolated stacks */
    sel4gpi_add_vmr_config(&cfg->ads_cfg,
                           GPI_DISJOINT, SEL4UTILS_RES_TYPE_STACK, NULL,
                           NULL, DEFAULT_STACK_PAGES, MO_PAGE_BITS, NULL);

    /* ELF code and data sections: shallow copy by type */
    sel4gpi_add_vmr_config(&cfg->ads_cfg, GPI_SHARED, SEL4UTILS_RES_TYPE_CODE, NULL, NULL, 0, 0, NULL);
    sel4gpi_add_vmr_config(&cfg->ads_cfg, GPI_SHARED, SEL4UTILS_RES_TYPE_DATA, NULL, NULL, 0, 0, NULL);

    /* heap: shallow copy by type */
    sel4gpi_add_vmr_config(&cfg->ads_cfg, GPI_SHARED, SEL4UTILS_RES_TYPE_HEAP, (void *)PD_HEAP_LOC, NULL, 0, 0, NULL);

    /* isolated IPC buffer */
    sel4gpi_add_vmr_config(&cfg->ads_cfg, GPI_DISJOINT, SEL4UTILS_RES_TYPE_IPC_BUF,
                           NULL, NULL, 1, MO_PAGE_BITS, NULL);

    /* give thread all our RDEs */
    sel4gpi_config_pd_share_all_rdes(cfg);

    cfg->ads_cfg.entry_point = isolated_thread;
    cfg->link_with_current = true;

    /* test if the thread can access our stack */
    uint64_t *frame_addr = (uint64_t *)__builtin_frame_address(0);
    error = sel4gpi_prepare_pd(cfg, &runnable, 1, (seL4_Word *)&frame_addr);
    test_assert(error == 0);

    error = sel4gpi_start_pd(&runnable);
    test_assert(error == 0);

    /* wait for thread to complete */
    seL4_Word type = seL4_DebugCapIdentify(cfg->fault_ep.raw_endpoint);
    seL4_MessageInfo_t info = seL4_Recv(cfg->fault_ep.raw_endpoint, NULL);
    if (seL4_MessageInfo_get_label(info))
    {
        sel4utils_print_fault_message(info, "isolated_thread fault");
    }

    if (*frame_addr == GARBAGE_DATA)
    {
        printf("Thread succeeded in garbling our stack frame!!\n");
    }
    else
    {
        printf("attempt to garble main thread's stack has been foiled!!\n");
    }

    sel4gpi_config_destroy(cfg);

    /* tell parent we've completed */
    printf("exiting hello_isolated_threads %s\n", __func__);
    seL4_MessageInfo_t msg = seL4_MessageInfo_new(0, 0, 0, 0);
    seL4_Send(fault_ep_conn.raw_endpoint, msg);

err_goto:
    return error;
}
