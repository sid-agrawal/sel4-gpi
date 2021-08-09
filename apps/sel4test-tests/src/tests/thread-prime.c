/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sel4test/test.h>
#include "../test.h"
#include "../helpers.h"

#define MIN_EXPECTED_ALLOCATIONS 100

int test_vspace_walker(env_t env)
{
    /* Perform a bunch of allocations and frees */
    vka_object_t endpoint;
    int error;

    for (int i = 0; i < MIN_EXPECTED_ALLOCATIONS; i++) {
        error = vka_alloc_endpoint(&env->vka, &endpoint);
        test_error_eq(error, 0);
        test_assert(endpoint.cptr != 0);
        vka_free_object(&env->vka, &endpoint);
    }

    return sel4test_get_result();
}
DEFINE_TEST(WALKER001, "Ensure the walker works", test_vspace_walker, true)