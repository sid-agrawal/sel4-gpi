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

int test_new_vmm_native(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);
    vm_context_t *vm;
    error = vm_native_setup(env->irq_handler, &env->vka, &env->vspace, env->page_directory, env->asid_pool, &env->simple, &vm);
    test_error_eq(error, 0);

#ifdef CONFIG_DEBUG_BUILD
    seL4_DebugDumpScheduler();
#endif
    /* temporary indefinite yield */
    while (1)
    {
        seL4_Yield();
    }

    // TODO destroy function that frees all the objects inside the vm context as well
    free(vm);

    return sel4test_get_result();
}

int test_new_vmm_osm(env_t env)
{
    int error = 0;
    printf("------------------STARTING: %s------------------\n", __func__);

    return sel4test_get_result();
}

DEFINE_TEST(GPIVM001, "Test native VMM", test_new_vmm_native, true)
DEFINE_TEST(GPIVM002, "Test OSM VMM", test_new_vmm_osm, true)
