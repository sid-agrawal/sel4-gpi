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

#define TEST_LOG(msg, ...)                                  \
    do                                                      \
    {                                                       \
        printf("%s(): " msg "\n", __func__, ##__VA_ARGS__); \
    } while (0)

static void test_thread(void)
{
    printf("In test thread\n");
}

int test_separate_threads(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    pd_client_context_t test_pd_os_cap;
    test_pd_os_cap.badged_server_ep_cspath.capPtr = sel4gpi_get_pd_cap();

    ads_client_context_t test_ads_os_cap;
    test_ads_os_cap.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_ns_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_ADS);

    seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
    seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);
    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);
    seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);

    /* Create a new ADS obj */
    seL4_CPtr slot;
    error = pd_client_next_slot(&test_pd_os_cap, &slot);

    // ads_client_context_t new_ads;
    // error = ads_component_client_connect(ads_rde, slot, &new_ads);

    /* Create a new CPU obj */
    error = pd_client_next_slot(&test_pd_os_cap, &slot);
    test_error_eq(error, 0);

    cpu_client_context_t new_cpu;
    error = cpu_component_client_connect(cpu_rde, slot, &new_cpu);
    test_error_eq(error, 0);

    /* allocate stack frame */
    error = pd_client_next_slot(&test_pd_os_cap, &slot);
    test_error_eq(error, 0);

    mo_client_context_t stack_mo;
    error = mo_component_client_connect(mo_rde, slot, 16, &stack_mo);
    test_error_eq(error, 0);

    /* attach stack to cpu */
    void *stack_addr_in_new_cpu;
    error = ads_client_attach(&test_ads_os_cap, NULL, &stack_mo, &stack_addr_in_new_cpu);
    test_error_eq(error, 0);

    /* configure cpu */
    ads_client_context_t test_ads_resource;
    test_ads_resource.badged_server_ep_cspath.capPtr = sel4gpi_get_ads_cap();
    error = cpu_client_config(&new_cpu, &test_ads_resource, NULL, env->cspace_root, 0, 0, (seL4_Word)stack_addr_in_new_cpu);
    test_error_eq(error, 0);

    // pd_client_dump(&test_pd_os_cap, NULL, 0);
    error = cpu_client_start(&new_cpu, (sel4utils_thread_entry_fn)&test_thread);
    test_error_eq(error, 0);

    return sel4test_get_result();
}
DEFINE_TEST(GPITH001,
            "Test Multiple Threads in PD",
            test_separate_threads,
            true)