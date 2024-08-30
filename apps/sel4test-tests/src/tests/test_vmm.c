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

#include <sel4debug/register_dump.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/pd_utils.h>
#include <sel4bench/arch/sel4bench.h>
#include <sel4/sel4.h>
#include <vka/capops.h>

#if SEL4TEST_VMM
#include <gpivmm/sel4test-vmm.h>

static int start_vmm_and_guest(env_t env, const char *guest_name)
{
    int error;

    // test process will act as the VMM
    error = sel4test_vmm_init(sel4test_get_irq_handler(env, SERIAL_IRQ),
                              &env->vka, &env->vspace, env->asid_pool,
                              &env->simple, env->tcb, env->endpoint);
    test_error_eq(error, 0);
    uint32_t guest_id = sel4test_new_guest(guest_name);
    test_assert(guest_id != 0);

    return error;
}

int test_hello_vm_sel4test(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    // test process will act as the VMM
    start_vmm_and_guest(env, HELLO_KERNEL_NAME);
    sel4test_sleep(env, 2UL * NS_IN_S);

    return sel4test_get_result();
}
DEFINE_TEST(GPIVM001, "Test VMM that starts a Hello guest PD (sel4test)", test_hello_vm_sel4test, true)

int test_linux_vm_sel4test(env_t env)
{
    printf("------------------STARTING: %s------------------\n", __func__);

    start_vmm_and_guest(env, LINUX_KERNEL_NAME);

    while (1)
    {
        sel4test_sleep(env, 10UL * NS_IN_S);
        seL4_Yield();
    }

    return sel4test_get_result();
}

DEFINE_TEST(GPIVM002, "Test VMM that starts one Linux guest (sel4test)", test_linux_vm_sel4test, true)
#endif

#ifdef OSM_VMM
#include <gpivmm/osm-vmm.h>

static int start_vmm_and_guest(const char *guest_name)
{
    int error = osm_vmm_init();
    test_error_eq(error, 0);

    uint32_t guest_id = osm_new_guest(guest_name);
    test_assert(guest_id != 0);

    return error;
}

int test_hello_vm_osm(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    start_vmm_and_guest(HELLO_KERNEL_NAME);

    sel4test_sleep(env, 2UL * NS_IN_S);

    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIVM003, "Test VMM that starts one Linux guest PD (osm)", test_hello_vm_osm, true)

int test_linux_vm_osm(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    start_vmm_and_guest(LINUX_KERNEL_NAME);

    while (1)
    {
        sel4test_sleep(env, 10UL * NS_IN_S);
    }

    return sel4test_get_result();
}

DEFINE_TEST_OSM(GPIVM004, "Test VMM that starts one Linux guest PD (osm)", test_linux_vm_osm, true)
#endif
