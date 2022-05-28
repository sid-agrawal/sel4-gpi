        /*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sel4test/test.h>
#include <sel4test/macros.h>
#include <sel4utils/thread.h>
#include "../test.h"
#include "../helpers.h"
#include <stdio.h>

#include<sel4gpi/pd_clientapi.h>

int test_new_process(env_t env)
{
    int error;
    
    // Make new PD i.e. CSspace
    pd_client_context_t conn;
    error = pd_component_client_connect(env->gpi_endpoint, &env->vka, &conn);
    assert(error == 0);


    // Make a new AS, loads an image
    error = pd_client_load(&conn, "hello");
    assert(error == 0);

    // Start it.
    error = pd_client_start(&conn);
    assert(error == 0);

    return sel4test_get_result();
}
DEFINE_TEST(GPIPD001, "Ensure that as new process works", test_new_process, true)