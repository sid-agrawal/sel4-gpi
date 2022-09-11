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
#include "../test.h"
#include "../helpers.h"
#include <stdio.h>

#include<sel4gpi/pd_clientapi.h>
#include <sel4bench/arch/sel4bench.h>

int test_new_process_osmosis(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    sel4bench_init();
    // Make new PD i.e. CSspace
    ccnt_t start;
    SEL4BENCH_READ_CCNT(start);


    /* create an endpoint for the parent to listen on*/
    vka_object_t ep_object = {0};
    error = vka_alloc_endpoint(&env->vka, &ep_object);
    assert(!error);


    /* Create a new PD */
    pd_client_context_t conn;
    error = pd_component_client_connect(env->gpi_endpoint, &env->vka, &conn);
    assert(error == 0);

    // Make a new AS, loads an image
    error = pd_client_load(&conn, "hello");
    assert(error == 0);

    // Copy the ep_object to the new PD
    seL4_Word slot;
    error = pd_client_send_cap(&conn, ep_object.cptr, &slot);
    assert(error == 0);

    // Start it.
    error = pd_client_start(&conn, slot); // with this arg.
    assert(error == 0);

    // Wait for it to finish.
    seL4_Word sender_badge = 0;
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(ep_object.cptr, NULL);
    ccnt_t end = seL4_GetMR(0);


    printf("root-task: Time to start process: %lu\n", end - start);

    /* modify the message */
    seL4_SetMR(0, 0xdeadbeef);
    printf("------------------ENDING: %s------------------\n", __func__);
    seL4_ReplyRecv(ep_object.cptr, tag, NULL);
    return sel4test_get_result();
}
DEFINE_TEST(GPIPD001, "OSMO: Ensure that as new process works", test_new_process_osmosis, true)