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

int test_counter_capability(env_t env)
{

    // Using a known EP, get a new counter CAP.

    // Increment the counter cap.

    // Delete the counter cap.
    counter_client_context_t conn;
    int error = counter_server_client_connect(env->counter_endpoint, &env->vka, &conn);
    test_error_eq(error, 0);

    error = counter_client_increment(&conn);
    test_error_eq(error, 0);

    return sel4test_get_result();
}
DEFINE_TEST(WALKER001, "Ensure the counter cap works works", test_counter_capability, true)
