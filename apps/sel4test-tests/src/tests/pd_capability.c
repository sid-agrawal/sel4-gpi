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
#include <sel4gpi/cpu_clientapi.h>
#include <sel4bench/arch/sel4bench.h>
#include <utils/uthash.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/pd_creation.h>

#define TEST_LOG(msg, ...)                                  \
    do                                                      \
    {                                                       \
        printf("%s(): " msg "\n", __func__, ##__VA_ARGS__); \
    } while (0)

int test_new_process_osmosis_shmem(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);
    pd_client_context_t new_pd;
    pd_resource_config_t *proc_cfg = sel4gpi_configure_process("hello", DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES, &new_pd);
    test_assert(proc_cfg != NULL);

    vka_object_t ep;
    error = vka_alloc_endpoint(&env->vka, &ep);
    test_error_eq(error, 0);

    seL4_CPtr slot;
    error = pd_client_send_cap(&new_pd, ep.cptr, &slot);
    test_error_eq(error, 0);

    sel4gpi_runnable_t runnable = {.pd = new_pd};
    error = sel4gpi_start_pd(proc_cfg, &runnable, 1, &slot);
    test_error_eq(error, 0);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    cspacepath_t ipc_cap;
    error = vka_cspace_alloc_path(&env->vka, &ipc_cap);
    test_error_eq(error, 0);

    seL4_SetCapReceivePath(ipc_cap.root, ipc_cap.capPtr, ipc_cap.capDepth);
    seL4_Recv(ep.cptr, NULL);

    pd_client_context_t self_pd = sel4gpi_get_pd_conn();
    error = pd_client_next_slot(&self_pd, &slot);
    test_error_eq(error, 0);

    mo_client_context_t shared_mo;
    error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO), slot, 1, &shared_mo);
    test_error_eq(error, 0);

    error = pd_client_send_cap(&new_pd, shared_mo.badged_server_ep_cspath.capPtr, &slot);
    test_error_eq(error, 0);

    seL4_SetMR(0, slot);
    tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_ReplyRecv(ep.cptr, tag, NULL);

    free(proc_cfg);

    return sel4test_get_result();
}
DEFINE_TEST(GPIPD001,
            "OSMO: Ensure that as new process works w/ SHMEM",
            test_new_process_osmosis_shmem,
            true)

int test_pd_dump(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    // Check that PD dump works on self, more than once
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();

    error = pd_client_dump(&pd_conn, NULL, 0);
    test_assert(error == 0);

    error = pd_client_dump(&pd_conn, NULL, 0);
    test_assert(error == 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}

DEFINE_TEST(GPIPD003, "Test PD dump", test_pd_dump, true)