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

#include<sel4gpi/pd_clientapi.h>
#include <sel4bench/arch/sel4bench.h>
#include <sel4/sel4.h>
#include <vka/capops.h>
#include <vmm/vmm.h>


int test_new_vmm(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);
    
    vmm_env_t *vmm_e = vm_setup(env->irq_handler, &env->vka, &env->vspace, env->page_directory, env->asid_pool);
    vm_init(vmm_e);

    seL4_DebugDumpScheduler();

    while (1) {
        seL4_Yield();
    }
    
    return sel4test_get_result();
}
DEFINE_TEST(GPIVM001, "Test running VM", test_new_vmm, true)