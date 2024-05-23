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
#include <sel4bench/arch/sel4bench.h>
#include <utils/uthash.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/pd_creation.h>
#include <sel4runtime.h>

extern __thread int __sel4gpi_osm_data;

#define TEST_LOG(msg, ...)                                  \
    do                                                      \
    {                                                       \
        printf("%s(): " msg "\n", __func__, ##__VA_ARGS__); \
    } while (0)

static void test_thread(void *arg0, void *arg1, void *ipc_buf)
{
    sel4gpi_set_exit_cb();
    printf("In test thread: arg0: %ld\n", (int64_t)arg0);
    // printf("osm_data from TLS: %u\n", __sel4gpi_osm_data);
    printf("goodbye!\n");
}

int test_native_threads(env_t env)
{
    int error;
    sel4utils_thread_t thread;

    sel4utils_thread_config_t t_cfg = thread_config_default(&env->simple, env->cspace_root, api_make_guard_skip_word(seL4_WordBits - TEST_PROCESS_CSPACE_SIZE_BITS), env->endpoint, seL4_MaxPrio);
    error = sel4utils_configure_thread_config(&env->vka, &env->vspace, &env->vspace, t_cfg, &thread);

    test_error_eq(error, 0);

    error = sel4utils_start_thread(&thread, test_thread, (void *)1, (void *)2, 1);
    test_error_eq(error, 0);
    return sel4test_get_result();
}

int test_osm_threads(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    // printf("osm_data: %d\n", __sel4gpi_osm_data);

    pd_client_context_t test_pd_os_cap = sel4gpi_get_pd_conn();
    seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);

    /* new PD to represent the thread */
    seL4_CPtr slot;
    error = pd_client_next_slot(&test_pd_os_cap, &slot);
    test_error_eq(error, 0);

    pd_client_context_t thread_pd;
    error = pd_component_client_connect(pd_rde, slot, &thread_pd);
    test_error_eq(error, 0);

    pd_resource_config_t *cfg = sel4gpi_generate_thread_config(test_thread, env->endpoint);
    test_assert(cfg != NULL);

    sel4gpi_runnable_t runnable = {.pd = thread_pd};

    seL4_Word arg0 = 1;
    error = sel4gpi_start_pd(cfg, &runnable, 1, &arg0);

    // terrible sleep mechanism to allow thread to run bc we don't have usleep
    int i = 0;
    while (i < 100000)
    {
        seL4_Yield();
        i++;
    }

    free(cfg);
    // pd_client_dump(&test_pd_os_cap, NULL, 0);
    return sel4test_get_result();
}

DEFINE_TEST(GPITH001,
            "Test Multiple Threads in PD",
            test_osm_threads,
            true)

DEFINE_TEST(GPITH002,
            "Spawn a native thread",
            test_native_threads,
            true)