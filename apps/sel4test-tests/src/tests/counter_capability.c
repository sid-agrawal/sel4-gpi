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

#include<sel4gpi/counter_clientapi.h>

#define MIN_EXPECTED_ALLOCATIONS 100

int test_counter_capability_connect(env_t env)
{
    counter_client_context_t conn;
    int error = 0;
    
    // Using a known EP, get a new counter CAP.
    error = counter_server_client_connect(env->counter_endpoint, &env->vka, &conn);
    test_error_eq(error, 0);

    counter_client_context_t conn2;
    // Using a known EP, get a new counter CAP.
    error = counter_server_client_connect(env->counter_endpoint, &env->vka, &conn2);
    test_error_eq(error, 0);

    counter_client_context_t conn3;
    // Using a known EP, get a new counter CAP.
    error = counter_server_client_connect(env->counter_endpoint, &env->vka, &conn3);
    test_error_eq(error, 0);

    printf(COUNTERSERVC"%s:%d counter_endpoint is %d: ", __FUNCTION__, __LINE__, env->counter_endpoint);
    debug_cap_identify(env->counter_endpoint);
    return sel4test_get_result();
}
DEFINE_TEST(GPICOUNTER001, "Ensure the counter cap connects", test_counter_capability_connect, true)

int test_counter_capability(env_t env)
{
    printf(COUNTERSERVC"%s:%d counter_endpoint is %d: ", __FUNCTION__, __LINE__, env->counter_endpoint);
    debug_cap_identify(env->counter_endpoint);
    counter_client_context_t conn;
    // Using a known EP, get a new counter CAP.
    int error = counter_server_client_connect(env->counter_endpoint, &env->vka, &conn);
    test_error_eq(error, 0);

    // Increment the counter cap.
    error = counter_client_increment(&conn);
    test_error_eq(error, 0);

    // Decrement the cap. TODO(siagraw)
    // Delete the counter cap. TODO(siagraw)
    return sel4test_get_result();
}
DEFINE_TEST(GPICOUNTER002, "Ensure the counter cap works works", test_counter_capability, true)