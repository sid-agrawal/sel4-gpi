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
    vm_context_t *vm;
    error = vm_native_setup(env->irq_handler, &env->vka, &env->vspace, env->page_directory, env->asid_pool, &env->simple, &vm);
    test_error_eq(error, 0);

    /* temporary indefinite yield */
    while (1)
    {
        seL4_Yield();
    }

    // TODO destroy function that frees all the objects inside the vm context as well
    // free(vm);
#elif OSM_VMM
    error = new_guest();
    test_error_eq(error, 0);
#endif

#ifdef CONFIG_DEBUG_BUILD
    seL4_DebugDumpScheduler();
#endif

    return sel4test_get_result();
}

DEFINE_TEST(GPIVM001, "Test VMM that starts one Linux guest", test_vmm, true)
