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

#include<sel4gpi/ads_clientapi.h>
#include<sel4gpi/cpu_clientapi.h>

int test_new_process(env_t env)
{
    int error;
    cspacepath_t path;
    vka_cspace_make_path(&env->vka, env->self_as_cptr, &path);
    ads_client_context_t conn;
    conn.badged_server_ep_cspath = path;

    // Make new PD i.e. CSspace

    // Make a new AS, loads an image

    // Start it.

    return sel4test_get_result();
}
DEFINE_TEST(GPIPD001, "Ensure that as new process works", test_new_process, true)