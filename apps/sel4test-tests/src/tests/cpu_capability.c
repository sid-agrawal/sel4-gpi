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
extern void _start(void);
extern char *morecore_area;

#define TEST_LOG(msg, ...)                                  \
    do                                                      \
    {                                                       \
        printf("%s(): " msg "\n", __func__, ##__VA_ARGS__); \
    } while (0)

static void osm_thread(int argc, char **argv)
{
    printf("Made it to osm_thread, argc: %d\n", argc);
    if (argc > 0)
    {
        for (int i = 0; i < argc; i++)
        {
            printf("%lX\n", atol(argv[i]));
        }
    }

    // we will be terminated when our parent thread exits
    while (1)
        ;
}

static void sel4utils_thread(void *arg0, void *arg1, void *ipc_buf)
{
    printf("Made it to sel4utils_thread, goodbye!\n");
    seL4_CPtr tcb = (seL4_CPtr)arg0;
    seL4_TCB_Suspend(tcb);
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

    error = sel4utils_start_thread(&thread, sel4utils_thread, (void *)thread.tcb.cptr, (void *)NULL, 1);
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
    pd_config_t *cfg = sel4gpi_configure_thread(osm_thread, seL4_CapNull, &runnable);
    test_assert(cfg != NULL);

    seL4_Word argv = 0xa;
    error = sel4gpi_prepare_pd(cfg, &runnable, 1, &argv);
    test_assert(error == 0);

    error = sel4gpi_start_pd(&runnable);
    test_assert(error == 0);

    sel4test_sleep(env, 5 * MILLISECOND);
    sel4gpi_config_destroy(cfg);

#if EXTRACT_MODEL
    pd_client_dump(&runnable.pd, NULL, 0);
#endif
    return sel4test_get_result();
}

DEFINE_TEST_OSM(GPITH002,
                "Test Multiple Threads in PD",
                test_osm_threads,
                true);

int test_threads_isolated_stack(env_t env)
{
    int error;
    printf("------------------STARTING: %s------------------\n", __func__);

    sel4gpi_runnable_t runnable = {0};
    pd_config_t *cfg = sel4gpi_configure_process("hello_isolated_threads", DEFAULT_STACK_PAGES,
                                                 DEFAULT_HEAP_PAGES, &runnable);

    sel4gpi_add_rde_config(cfg, GPICAP_TYPE_PD, BADGE_SPACE_ID_NULL);
    sel4gpi_add_rde_config(cfg, GPICAP_TYPE_ADS, BADGE_SPACE_ID_NULL);
    sel4gpi_add_rde_config(cfg, GPICAP_TYPE_CPU, BADGE_SPACE_ID_NULL);
    sel4gpi_add_rde_config(cfg, GPICAP_TYPE_EP, BADGE_SPACE_ID_NULL);

    cfg->link_with_current = true;
    error = sel4gpi_prepare_pd(cfg, &runnable, 0, NULL);
    test_error_eq(error, 0);

    error = sel4gpi_start_pd(&runnable);
    test_error_eq(error, 0);

    /* wait for PD to notify completion */
    seL4_Recv(cfg->fault_ep.raw_endpoint, NULL);
    printf("exiting %s\n", __func__);
    sel4gpi_config_destroy(cfg);

#if EXTRACT_MODEL
    pd_client_dump(&runnable.pd, NULL, 0);
#endif
    return sel4test_get_result();
}

DEFINE_TEST_OSM(GPITH003,
                "Test threads with isolated stacks",
                test_threads_isolated_stack,
                true);
