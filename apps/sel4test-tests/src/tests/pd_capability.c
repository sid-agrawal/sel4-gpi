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

#define TEST_LOG(msg, ...)                                  \
    do                                                      \
    {                                                       \
        printf("%s(): " msg "\n", __func__, ##__VA_ARGS__); \
    } while (0)

int test_new_process_osmosis(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    sel4bench_init();
    // Make new PD i.e. CSspace
    ccnt_t start;
    SEL4BENCH_READ_CCNT(start);

    seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
    seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);
    seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);

    seL4_CPtr slot;
    error = vka_cspace_alloc(&env->vka, &slot);
    test_error_eq(error, 0);

    /* Create a new PD */
    pd_client_context_t pd_os_cap;
    error = pd_component_client_connect(pd_rde, slot, &pd_os_cap);
    test_error_eq(error, 0);

    /* Create a new ADS Cap, which will be in the context of a PD and image */
    error = vka_cspace_alloc(&env->vka, &slot);
    test_error_eq(error, 0);

    ads_client_context_t ads_os_cap;
    error = ads_component_client_connect(ads_rde, slot, &ads_os_cap, NULL);
    test_error_eq(error, 0);

    error = vka_cspace_alloc(&env->vka, &slot);
    test_error_eq(error, 0);

    cpu_client_context_t cpu_os_cap;
    error = cpu_component_client_connect(cpu_rde, slot, &cpu_os_cap);
    test_error_eq(error, 0);

    error = pd_client_share_rde(&pd_os_cap, GPICAP_TYPE_MO, NSID_DEFAULT);
    test_error_eq(error, 0);

    // Make a new AS, loads an image
    error = pd_client_load(&pd_os_cap, &ads_os_cap, &cpu_os_cap, "hello");
    test_error_eq(error, 0);

    // Copy the ep_object to the new PD
    // error = pd_client_send_cap(&pd_os_cap, ep_object.cptr, &slot);
    // test_error_eq(error, 0);

    /*
        (XXX) Create a new CPU cap, and make that the PD's primary CPU cap.
    */

    // Start the CPU.
    error = pd_client_start(&pd_os_cap, 0, 0); // with this arg.
    test_error_eq(error, 0);

    /*********************************************/

    /* Create a new PD */
    error = vka_cspace_alloc(&env->vka, &slot);
    test_error_eq(error, 0);

    pd_client_context_t pd_os_cap2;
    error = pd_component_client_connect(pd_rde, slot, &pd_os_cap2);
    test_error_eq(error, 0);

    /* Create a new ADS Cap, which will be in the context of a PD and image */
    error = vka_cspace_alloc(&env->vka, &slot);
    test_error_eq(error, 0);

    ads_client_context_t ads_os_cap2;
    error = ads_component_client_connect(ads_rde, slot, &ads_os_cap2, NULL);
    test_error_eq(error, 0);

    error = vka_cspace_alloc(&env->vka, &slot);
    test_error_eq(error, 0);

    cpu_client_context_t cpu_os_cap2;
    error = cpu_component_client_connect(cpu_rde, slot, &cpu_os_cap2);
    test_error_eq(error, 0);

    error = pd_client_share_rde(&pd_os_cap2, GPICAP_TYPE_MO, NSID_DEFAULT);
    test_error_eq(error, 0);

    // Make a new AS, loads an image
    error = pd_client_load(&pd_os_cap2, &ads_os_cap2, &cpu_os_cap2, "hello");
    test_error_eq(error, 0);

    // Start the CPU.
    error = pd_client_start(&pd_os_cap2, 0, 0); // with this arg.
    test_error_eq(error, 0);

    error = pd_client_dump(&pd_os_cap2, NULL, 0);
    test_error_eq(error, 0);

    error = pd_client_dump(&pd_os_cap, NULL, 0);
    test_error_eq(error, 0);

    /*********************************************/
    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}

DEFINE_TEST(GPIPD001, "OSMO: Ensure that as new process works", test_new_process_osmosis, true)

int test_new_process_osmosis_shmem(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

#if CONFIG_MAX_NUM_NODES > 1
    seL4_TCB_GetAffinity_t affinity = seL4_TCB_GetAffinity(env->tcb);
    TEST_LOG("affinity: %ld", affinity.affinity);
#endif // CONFIG_MAX_NUM_NODES > 1

    sel4bench_init();
    // Make new PD i.e. CSspace
    ccnt_t start;
    SEL4BENCH_READ_CCNT(start);

    pd_client_context_t test_pd_os_cap;
    test_pd_os_cap.badged_server_ep_cspath.capPtr = sel4gpi_get_pd_cap();

    ads_client_context_t test_ads_os_cap;
    test_ads_os_cap.badged_server_ep_cspath.capPtr = sel4gpi_get_ads_cap();

    seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
    seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);
    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);
    seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);

    /* new PD */
    seL4_CPtr slot;
    error = vka_cspace_alloc(&env->vka, &slot);
    test_error_eq(error, 0);
    pd_client_context_t pd_os_cap;
    error = pd_component_client_connect(pd_rde, slot, &pd_os_cap);
    test_error_eq(error, 0);

    /* new ADS */
    error = vka_cspace_alloc(&env->vka, &slot);
    test_error_eq(error, 0);
    ads_client_context_t ads_os_cap;
    seL4_Word new_ads_id;
    error = ads_component_client_connect(ads_rde, slot, &ads_os_cap, &new_ads_id);
    test_error_eq(error, 0);

    /* new CPU */
    error = vka_cspace_alloc(&env->vka, &slot);
    test_error_eq(error, 0);
    cpu_client_context_t cpu_os_cap;
    error = cpu_component_client_connect(cpu_rde, slot, &cpu_os_cap);
    test_error_eq(error, 0);

    void *entry_point;
    error = ads_client_load_elf(&ads_os_cap, &pd_os_cap, "hello", &entry_point);
    test_error_eq(error, 0);

    ads_client_context_t new_ads_rde = {.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_ns_id(new_ads_id, GPICAP_TYPE_ADS)};
    void *stack = sel4gpi_new_sized_stack(&new_ads_rde, 16);
    test_assert(stack != NULL);

    void *heap = sel4gpi_get_vmr(&new_ads_rde, 100, (void *)PD_HEAP_LOC, SEL4UTILS_RES_TYPE_HEAP, NULL);
    test_assert(heap != NULL);

    mo_client_context_t ipc_mo;
    void *ipc_buf = sel4gpi_get_vmr(&new_ads_rde, 1, NULL, SEL4UTILS_RES_TYPE_IPC_BUF, &ipc_mo);
    test_assert(ipc_buf != NULL);

    seL4_Word cnode_guard = api_make_guard_skip_word(seL4_WordBits - TEST_PROCESS_CSPACE_SIZE_BITS);
    error = cpu_client_config(&cpu_os_cap, &ads_os_cap, &ipc_mo, &pd_os_cap, cnode_guard, seL4_CapNull, (seL4_Word)ipc_buf);
    test_error_eq(error, 0);

    seL4_Word arg0 = 1;
    void *init_stack;
    error = ads_client_prepare_stack(&ads_os_cap, &pd_os_cap, stack, 16, 1, &arg0, &init_stack);
    test_error_eq(error, 0);

    error = pd_client_share_rde(&pd_os_cap, GPICAP_TYPE_MO, NSID_DEFAULT);
    test_error_eq(error, 0);

    error = cpu_client_start(&cpu_os_cap, entry_point, init_stack, 0);
    test_error_eq(error, 0);
#if 0
    // Make a new AS, loads an image
    error = pd_client_load(&pd_os_cap, &ads_os_cap, &cpu_os_cap, "hello");
    test_error_eq(error, 0);
    printf("Loaded hello\n");

    error = pd_client_share_rde(&pd_os_cap, GPICAP_TYPE_MO, NSID_DEFAULT);
    test_error_eq(error, 0);

    // Make a new MO cap
    cspacepath_t mo_cap_path;
    error = vka_cspace_alloc_path(&env->vka, &mo_cap_path);
    test_error_eq(error, 0);

    mo_client_context_t mo_conn_shared;
    error = mo_component_client_connect(mo_rde,
                                        mo_cap_path.capPtr,
                                        1,
                                        &mo_conn_shared);
    test_error_eq(error, 0);

    // Attach it to current AS
    ads_client_context_t test_ads_cap;
    test_ads_cap.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_ns_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_ADS);

    void *vaddr;
    error = ads_client_attach(&test_ads_cap,
                              0, /*vaddr*/
                              &mo_conn_shared,
                              SEL4UTILS_RES_TYPE_SHARED_FRAMES,
                              &vaddr);
    test_error_eq(error, 0);

    vka_object_t ep;
    error = vka_alloc_endpoint(&env->vka, &ep);
    test_error_eq(error, 0);

    error = pd_client_send_cap(&pd_os_cap, ep.cptr, &slot);
    test_error_eq(error, 0);
    // Start the CPU.
    error = pd_client_start(&pd_os_cap,
                            1,
                            &slot); // with this arg.
    test_error_eq(error, 0);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    cspacepath_t ipc_cap;
    error = vka_cspace_alloc_path(&env->vka, &ipc_cap);
    test_error_eq(error, 0);

    seL4_SetCapReceivePath(ipc_cap.root, ipc_cap.capPtr, ipc_cap.capDepth);
    tag = seL4_Recv(ep.cptr, NULL);

    error = pd_client_send_cap(&pd_os_cap,
                               mo_conn_shared.badged_server_ep_cspath.capPtr,
                               &slot);
    test_error_eq(error, 0);

    tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, slot);
    seL4_Send(ipc_cap.capPtr, tag);

    TEST_LOG("Sent MO to PD at slot %lx", slot);

    pd_client_dump(&test_pd_os_cap, NULL, 0);
    // pd_client_dump(&pd_os_cap, NULL, 0);
    // Hello PD should also attach it.


    // Wait for it to finish.
    seL4_Word sender_badge = 0;
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(ep_object.cptr, NULL);

    error = pd_client_dump(&pd_os_cap, NULL, 0);
    test_error_eq(error, 0);

    /* modify the message */
    seL4_SetMR(0, start);
    seL4_ReplyRecv(ep_object.cptr, tag, NULL);
    printf("------------ Phase 2: %s ------------\n", __FUNCTION__);
    while (1)
    {
        //  printf("main responding to other thread\n");
        seL4_ReplyRecv(ep_object.cptr, tag, NULL);
    }
    printf("------------------ENDING: %s------------------\n", __func__);
#endif
    return sel4test_get_result();
}
DEFINE_TEST(GPIPD002,
            "OSMO: Ensure that as new process works w/ SHMEM",
            test_new_process_osmosis_shmem,
            true)

int test_pd_dump(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    // Check that PD dump works on self, more than once
    pd_client_context_t pd_conn;
    vka_cspace_make_path(&env->vka, sel4gpi_get_pd_cap(), &pd_conn.badged_server_ep_cspath);

    error = pd_client_dump(&pd_conn, NULL, 0);
    test_assert(error == 0);

    error = pd_client_dump(&pd_conn, NULL, 0);
    test_assert(error == 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}

DEFINE_TEST(GPIPD003, "Test PD dump", test_pd_dump, true)