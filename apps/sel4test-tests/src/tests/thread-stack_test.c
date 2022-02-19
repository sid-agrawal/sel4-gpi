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

#define MIN_EXPECTED_ALLOCATIONS 100

int test_vspace_walker(env_t env)
{
    // siagraw: This is not the right vspace to look at.
    // int num_res = sel4utils_walk_vspace(&env->vspace, &env->vka);
    return sel4test_get_result();
}
DEFINE_TEST(WALKER001, "Ensure the walker works", test_vspace_walker, true)
