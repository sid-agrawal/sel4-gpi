/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sel4test/test.h>
#include <sel4test/macros.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/debug.h>

#include <sel4utils/thread.h>
#include <sel4gpi/debug.h>
#include "../test.h"
#include "../helpers.h"
#include <stdio.h>
#include <unistd.h>

#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/pd_utils.h>
#include <sel4bench/arch/sel4bench.h>
#include <sel4/sel4.h>
#include <vka/capops.h>
#include <sel4test-vmm/vmm.h>
#include <osm-vmm/vmm.h>

int test_vmm_native(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

#if SEL4TEST_VMM
    // test process will act as the VMM
    error = sel4test_vmm_init(env->irq_handler, &env->vka, &env->vspace,
                              env->asid_pool, &env->simple, env->tcb, env->endpoint);
    test_error_eq(error, 0);
    uint32_t guest_id = sel4test_new_guest();
    test_assert(guest_id != 0);

    while (1)
    {
        sel4test_sleep(env, 10UL * SECOND);
        // seL4_Yield();
    }
#else
    printf("SEL4TEST_VMM is disabled\n");
#endif

    // #ifdef CONFIG_DEBUG_BUILD
    //     seL4_DebugDumpScheduler();
    // #endif

    return sel4test_get_result();
}

int test_vmm_osm(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

#if OSM_VMM
    error = new_guest();
    test_error_eq(error, 0);

    while (1)
    {
        sel4test_sleep(env, 10UL * SECOND);
    }
#else
    printf("OSM_VMM is disabled\n");
#endif

    // #ifdef CONFIG_DEBUG_BUILD
    //     seL4_DebugDumpScheduler();
    // #endif

    return sel4test_get_result();
}

// Disable these because they are WIP
DEFINE_TEST(GPIVM001, "Test VMM that starts one Linux guest (native)", test_vmm_native, false)
DEFINE_TEST_OSM(GPIVM002, "Test VMM that starts one Linux guest (osm)", test_vmm_osm, false)
