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

int test_vmm(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);
#if SEL4TEST_VMM
    // test process will act as the VMM
    error = sel4test_vmm_init(env->irq_handler, &env->vka, &env->vspace,
                              env->asid_pool, &env->simple, env->tcb, env->endpoint);
    test_error_eq(error, 0);
    uint32_t guest_id = sel4test_new_guest();
    CPRINTF("here\n");
    test_assert(guest_id != 0);
#elif OSM_VMM
    error = new_guest();
    test_error_eq(error, 0);
#endif

    while (1)
    {
        seL4_Yield();
    }

#ifdef CONFIG_DEBUG_BUILD
    seL4_DebugDumpScheduler();
#endif

    return sel4test_get_result();
}

DEFINE_TEST(GPIVM001, "Test VMM that starts one Linux guest", test_vmm, true)
