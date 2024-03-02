/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

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

#define TEST_LOG(msg, ...) \
    do { printf("%s(): " msg "\n", __func__, ##__VA_ARGS__); } while (0)
   
int test_new_process_osmosis(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    sel4bench_init();
    // Make new PD i.e. CSspace
    ccnt_t start;
    seL4_Word slot;
    SEL4BENCH_READ_CCNT(start);

    /* Create a new PD */
    pd_client_context_t pd_os_cap;
    error = pd_component_client_connect(env->gpi_endpoint, &env->vka, &pd_os_cap);
    assert(error == 0);

    /* Create a new ADS Cap, which will be in the context of a PD and image */
    ads_client_context_t ads_os_cap;
    error = ads_component_client_connect(env->gpi_endpoint, &env->vka, &ads_os_cap);
    assert(error == 0);

    /*
        (XXX)
        Give the PD some RDEs
        {
            "VA": "slot",
            "vCPU": "slot",
            ...
        }
    */

    // Make a new AS, loads an image
    error = pd_client_load(&pd_os_cap, &ads_os_cap, "hello");
    assert(error == 0);

    // Copy the ep_object to the new PD
    // error = pd_client_send_cap(&pd_os_cap, ep_object.cptr, &slot);
    // assert(error == 0);

    /*
        (XXX) Create a new CPU cap, and make that the PD's primary CPU cap.
    */

    // Start the CPU.
    error = pd_client_start(&pd_os_cap,
                            /* The (ADS, CPU) tuple to use */
                            slot); // with this arg.
    assert(error == 0);

    /*********************************************/

    /* Create a new PD */
    pd_client_context_t pd_os_cap2;
    error = pd_component_client_connect(env->gpi_endpoint, &env->vka, &pd_os_cap2);
    assert(error == 0);

    /* Create a new ADS Cap, which will be in the context of a PD and image */
    ads_client_context_t ads_os_cap2;
    error = ads_component_client_connect(env->gpi_endpoint, &env->vka, &ads_os_cap2);
    assert(error == 0);

    /*
        Give the PD some RDEs
        {
            "VA": "slot",
            "vCPU": "slot",
            ...
        }
    */

    // Make a new AS, loads an image
    error = pd_client_load(&pd_os_cap2, &ads_os_cap2, "hello");
    assert(error == 0);

    // Start the CPU.
    error = pd_client_start(&pd_os_cap2,
                            /* The (ADS, CPU) tuple to use */
                            slot); // with this arg.
    assert(error == 0);

    error = pd_client_dump(&pd_os_cap2, NULL, 0);
    assert(error == 0);

    error = pd_client_dump(&pd_os_cap, NULL, 0);
    assert(error == 0);

    /*********************************************/
    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}

DEFINE_TEST(GPIPD001, "OSMO: Ensure that as new process works", test_new_process_osmosis, true)

int test_new_process_osmosis_shmem(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    sel4bench_init();
    // Make new PD i.e. CSspace
    ccnt_t start;
    SEL4BENCH_READ_CCNT(start);

    /* Create a new PD */
    pd_client_context_t pd_os_cap;
    error = pd_component_client_connect(env->gpi_endpoint, &env->vka, &pd_os_cap);
    assert(error == 0);

    /* Create a new ADS Cap, which will be in the context of a PD and image */
    ads_client_context_t ads_os_cap;
    error = ads_component_client_connect(env->gpi_endpoint, &env->vka, &ads_os_cap);
    assert(error == 0);

    // Make a new AS, loads an image
    error = pd_client_load(&pd_os_cap, &ads_os_cap, "hello");
    assert(error == 0);
    printf("Loaded hello\n");

#if 1
    // Copy the ep_object to the new PD
    seL4_CPtr slot;
    error = pd_client_next_slot(&pd_os_cap, &slot);
    assert(error == 0);
    printf("Next free slot is %ld\n", (seL4_Word)slot);

    mo_client_context_t mo_conn;
    error = mo_component_client_connect(env->gpi_endpoint,
                                        slot,
                                        5,
                                        &mo_conn);
    test_error_eq(error, 0);

    /* request some EP cap from the gpi-server*/
    error = pd_client_send_cap(&pd_os_cap,
                               mo_conn.badged_server_ep_cspath.capPtr,
                               &slot);
    test_error_eq(error, 0);

    // Create a new CPU cap, and make that the PD's primary cap.
    void *ret_vaddr;
    error = ads_client_attach(&ads_os_cap,
                              0, /*vaddr*/
                              &mo_conn,
                              &ret_vaddr);
    assert(error == 0);

#if 1
    // Start the CPU.
    error = pd_client_start(&pd_os_cap,
                            /* The (ADS, CPU) tuple to use */
                            slot); // with this arg.
    assert(error == 0);
#endif

    // Make a new MO cap
    cspacepath_t mo_cap_path;
    error = vka_cspace_alloc_path(&env->vka, &mo_cap_path);
    test_error_eq(error, 0);

    mo_client_context_t mo_conn_shared;
    error = mo_component_client_connect(env->gpi_endpoint,
                                        mo_cap_path.capPtr,
                                        1,
                                        &mo_conn_shared);
    test_error_eq(error, 0);

    // Attach it to current AS
    ads_client_context_t test_ads_cap;
    vka_cspace_make_path(&env->vka, env->self_ads_cptr, &test_ads_cap.badged_server_ep_cspath);

    void *vaddr;
    error = ads_client_attach(&test_ads_cap,
                              0, /*vaddr*/
                              &mo_conn_shared,
                              &vaddr);
    test_error_eq(error, 0);

    // Send it to "hello" PD
    error = pd_client_send_cap(&pd_os_cap,
                               mo_conn_shared.badged_server_ep_cspath.capPtr,
                               &slot);
    test_error_eq(error, 0);

    TEST_LOG("mo slot: %ld", slot);
#endif
    // Hello PD should also attach it.
#if 0

    // Wait for it to finish.
    seL4_Word sender_badge = 0;
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(ep_object.cptr, NULL);

    error = pd_client_dump(&pd_os_cap, NULL, 0);
    assert(error == 0);

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