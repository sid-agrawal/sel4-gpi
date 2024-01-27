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

int test_new_vmm(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);
    sel4utils_process_config_t config = process_config_default_simple(&env->simple, "vmm", env->priority);
    sel4utils_process_t p;
    error = sel4utils_configure_process_custom(&p, &env->vka, &env->vspace, config);
    assert(error == 0);

    char *argv[2];
    argv[0] = "hello";
    argv[1] = "world";
    error = sel4utils_spawn_process_v(&p, &env->vka, &env->vspace, 1, &argv, 1);
    assert(error == 0);

    while (1)
    {
        seL4_Yield();
    }
    
    return sel4test_get_result();
}
DEFINE_TEST(GPIVM001, "Test running VM", test_new_vmm, true)