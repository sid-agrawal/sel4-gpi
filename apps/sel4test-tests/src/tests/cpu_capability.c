/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sel4/sel4.h>
#include <sel4test/test.h>
#include <sel4test/macros.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/debug.h>

#include <vka/capops.h>

#include <sel4utils/thread.h>
#include <sel4gpi/debug.h>
#include "../test.h"
#include "../helpers.h"
#include <stdio.h>

#include <sel4gpi/pd_clientapi.h>
#include <sel4bench/arch/sel4bench.h>
#include <utils/uthash.h>
#include <sel4gpi/pd_utils.h>
#include <sel4runtime.h>

#define TEST_LOG(msg, ...)                                  \
    do                                                      \
    {                                                       \
        printf("%s(): " msg "\n", __func__, ##__VA_ARGS__); \
    } while (0)

static void test_thread(void *arg0, void *arg1, void *ipc_buf)
{
    printf("In test thread: arg0: %ld\n", (int64_t)arg0);
}

int test_separate_threads(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    pd_client_context_t test_pd_os_cap = sel4gpi_get_pd_conn();

    ads_client_context_t test_ads_os_cap;
    test_ads_os_cap.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_space_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_VMR);

    seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
    seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);
    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);
    seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);

    /* new PD to represent the thread */
    seL4_CPtr slot;
    error = pd_client_next_slot(&test_pd_os_cap, &slot);
    test_error_eq(error, 0);

    pd_client_context_t thread_pd;
    error = pd_component_client_connect(pd_rde, slot, &thread_pd);
    test_error_eq(error, 0);

    pd_resource_config_t *cfg = sel4gpi_generate_thread_config();
    test_assert(cfg != NULL);

    sel4gpi_runnable_t runnable = {.pd = thread_pd};

    seL4_Word arg0 = 1;
    error = sel4gpi_start_pd(cfg, &runnable, 1, &arg0);
    free(cfg);
#if 0
    /* Create a new CPU obj */
    error = pd_client_next_slot(&test_pd_os_cap, &slot);
    test_error_eq(error, 0);

    cpu_client_context_t new_cpu;
    error = cpu_component_client_connect(cpu_rde, slot, &new_cpu);
    test_error_eq(error, 0);

    /* allocate stack frame */
    void *stack = sel4gpi_new_sized_stack(&test_ads_os_cap, DEFAULT_STACK_PAGES);
    test_assert(stack != NULL);

    /* prepare stack */
    ads_client_context_t test_ads_resource;
    test_ads_resource.badged_server_ep_cspath.capPtr = sel4gpi_get_ads_cap();
    seL4_Word cnode_guard = api_make_guard_skip_word(seL4_WordBits - TEST_PROCESS_CSPACE_SIZE_BITS);

    seL4_Word arg = 2;
    void *init_stack;
    error = ads_client_pd_setup(&test_ads_resource, &thread_pd, stack, DEFAULT_STACK_PAGES, 1, &arg, ADS_THREAD, &init_stack);
    test_error_eq(error, 0);
    test_assert(init_stack != NULL);

    /* configure cpu */
    error = cpu_client_config(&new_cpu, &test_ads_resource, NULL, &thread_pd, cnode_guard, 0, 0);
    test_error_eq(error, 0);

    //uintptr_t aligned_stack_pointer = sel4gpi_setup_thread_stack(stack_addr_in_new_cpu, 16);
    error = cpu_client_start(&new_cpu, &test_thread, init_stack, 0);
    test_error_eq(error, 0);

    // terrible sleep mechanism to allow thread to run bc we don't have usleep
    int i = 0;
    while (i < 100000)
    {
        seL4_Yield();
        i++;
    }
#endif
    // pd_client_dump(&test_pd_os_cap, NULL, 0);
    return sel4test_get_result();
}
DEFINE_TEST(GPITH001,
            "Test Multiple Threads in PD",
            test_separate_threads,
            true)