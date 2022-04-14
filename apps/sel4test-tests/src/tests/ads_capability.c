        /*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sel4test/test.h>
#include <sel4test/macros.h>
#include "../test.h"
#include "../helpers.h"
#include <stdio.h>

#include<sel4gpi/ads_clientapi.h>

int test_ads_clone(env_t env)
{
    cspacepath_t path;
    vka_cspace_make_path(&env->vka, env->self_as_cptr, &path);
    ads_client_context_t conn;
    conn.badged_server_ep_cspath = path;

    // Using a known EP, get a new ads CAP.
    ads_client_context_t conn_clone;
    int error = ads_client_clone(&conn, &env->vka,  0x10001000, &conn_clone);
    test_error_eq(error, 0);

    // Decrement the cap. TODO(siagraw)
    // Delete the ads cap. TODO(siagraw)
    return sel4test_get_result();
}
DEFINE_TEST(GPIADS001, "Ensure the ads clone works", test_ads_clone, true)

#ifdef WQEAREREADY
int test_ads_attach(env_t env)
{
    ads_client_context_t conn;
    // Using a known EP, get a new ads CAP.
    int error = ads_server_client_connect(env->ads_endpoint, &env->vka, &conn);
    test_error_eq(error, 0);

    // Increment the ads cap.
    error = ads_client_attach(&conn, 0, 0, 0);
    test_error_eq(error, 0);

    // Decrement the cap. TODO(siagraw)
    // Delete the ads cap. TODO(siagraw)
    return sel4test_get_result();
}
DEFINE_TEST(GPIADS002, "Ensure the ads attach works", test_ads_attach, true)

int test_ads_bind_cpu(env_t env)
{
    ads_client_context_t conn;
    // Using a known EP, get a new ads CAP.
    int error = ads_server_client_connect(env->ads_endpoint, &env->vka, &conn);
    test_error_eq(error, 0);

    // Increment the ads cap.
    seL4_TCB tcb = 0; // Get a new CPU cap
    error = ads_client_bind_cpu(&conn, tcb);
    test_error_eq(error, 0);

    // Decrement the cap. TODO(siagraw)
    // Delete the ads cap. TODO(siagraw)
    return sel4test_get_result();
}
DEFINE_TEST(GPIADS003, "Ensure the ads bind to cpu works", test_ads_bind_cpu, true)

int test_ads_clone(env_t env)
{
    ads_client_context_t conn;
    // Using a known EP, get a new ads CAP.
    int error = ads_server_client_connect(env->ads_endpoint, &env->vka, &conn);
    test_error_eq(error, 0);

    // Increment the ads cap.
    error = 0 ;// Call clone
    test_error_eq(error, 0);

    // Decrement the cap. TODO(siagraw)
    // Delete the ads cap. TODO(siagraw)
    return sel4test_get_result();
}
DEFINE_TEST(GPIADS004, "Ensure the ads clone works", test_ads_clone, true)

int test_ads_stack_isolated(env_t env)
{
    ads_client_context_t conn;
    // Using a known EP, get a new ads CAP.
    int error = ads_server_client_connect(env->ads_endpoint, &env->vka, &conn);
    test_error_eq(error, 0);

    // Clone the ads,
    // ads_client_clone(&conn, 0, 0, 0);
    // Attach a new stack
    // stack_cap
    // ads_client_clone(&conn, 0, 0, 0);
    // Attach a new stack
    // Allocate a new PD i.e. cspace.
    // Allocate a new TCB and attach this ADS to it.

    // Decrement the cap. TODO(siagraw)
    // Delete the ads cap. TODO(siagraw)
    return sel4test_get_result();
}
DEFINE_TEST(GPIADS005, "Ensure the threads with isolated stack works", test_ads_stack_isolated, true)

#endif

