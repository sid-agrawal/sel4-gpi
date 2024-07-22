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

extern __thread void *__sel4gpi_osm_data;

#define TEST_LOG(msg, ...)                                  \
    do                                                      \
    {                                                       \
        printf("%s(): " msg "\n", __func__, ##__VA_ARGS__); \
    } while (0)

static void test_thread(void *arg0, void *arg1, void *arg2)
{
    bool is_osm_thread = (bool)arg0;
    printf("In test thread, OSM thread? %d, %s: %lX, arg2: %ld\n",
           is_osm_thread,
           is_osm_thread ? "CPU" : "TCB",
           (uint64_t)arg1,
           (uint64_t)arg2);
    printf("goodbye!\n");

    if (is_osm_thread)
    {
        cpu_client_context_t self_cpu = sel4gpi_get_cpu_conn();
        cpu_client_suspend(&self_cpu);
    }
    else
    {
        seL4_CPtr tcb = (seL4_CPtr)arg1;
        seL4_TCB_Suspend(tcb);
    }
}

int test_native_threads(env_t env)
{
    int error;
    sel4utils_thread_t thread;

    sel4utils_thread_config_t t_cfg = thread_config_default(&env->simple,
                                                            env->cspace_root,
                                                            api_make_guard_skip_word(seL4_WordBits - TEST_PROCESS_CSPACE_SIZE_BITS),
                                                            env->endpoint,
                                                            seL4_MaxPrio);
    error = sel4utils_configure_thread_config(&env->vka, &env->vspace, &env->vspace, t_cfg, &thread);

    test_error_eq(error, 0);

    error = sel4utils_start_thread(&thread, test_thread, (void *)false, (void *)thread.tcb.cptr, 1);
    test_error_eq(error, 0);

    sel4test_sleep(env, 5 * MILLISECOND);

    sel4utils_clean_up_thread(&env->vka, &env->vspace, &thread);

    return sel4test_get_result();
}

DEFINE_TEST(GPITH001,
            "Spawn a native thread",
            test_native_threads,
            true);

int test_osm_threads(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    sel4gpi_runnable_t runnable = {0};
    pd_config_t *cfg = sel4gpi_configure_thread(test_thread, seL4_CapNull, &runnable);
    test_assert(cfg != NULL);

    seL4_Word args = true;
    error = sel4gpi_prepare_pd(cfg, &runnable, 1, &args);
    test_assert(error == 0);

    error = sel4gpi_start_pd(&runnable);
    test_assert(error == 0);

    sel4test_sleep(env, 5 * MILLISECOND);
    sel4gpi_config_destroy(cfg);

    // pd_client_dump(&test_pd_os_cap, NULL, 0);
    return sel4test_get_result();
}

// (XXX) Linh: this test current causes a memory leak with the thread-PD, since we have not
// implemented a config option to clean up children PD when the parent PD exits
DEFINE_TEST_OSM(GPITH002,
                "Test Multiple Threads in PD",
                test_osm_threads,
                false);