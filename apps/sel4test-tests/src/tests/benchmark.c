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
#include <sel4gpi/bench_utils.h>

#define TEST_LOG(msg, ...)                                  \
    do                                                      \
    {                                                       \
        printf("%s(): " msg "\n", __func__, ##__VA_ARGS__); \
    } while (0)

static int spawn_pd_native(env_t env, vka_object_t **ret_ep)
{
    int error;
    sel4utils_process_t proc;
    sel4utils_process_config_t config = process_config_default_simple(&env->simple, "hello_benchmark", env->priority);
    config = process_config_mcp(config, seL4_MaxPrio);
    config = process_config_auth(config, simple_get_tcb(&env->simple));
    config = process_config_create_cnode(config, 12);
    error = sel4utils_configure_process_custom(&proc, &env->vka, &env->vspace, config);
    test_error_eq(error, 0);

    vka_object_t *ep = malloc(sizeof(vka_object_t));
    error = vka_alloc_endpoint(&env->vka, ep);
    test_error_eq(error, 0);
    *ret_ep = ep;

    // cspacepath_t src, dest;
    // vka_cspace_make_path(&env->vka, ep.cptr, &src);
    seL4_CPtr ep_slot = sel4utils_copy_cap_to_process(&proc, &env->vka, ep->cptr);

    seL4_Word argc = 2;
    char string_args[argc][WORD_STRING_SIZE];
    char *argv[argc];
    sel4utils_create_word_args(string_args, argv, argc, ep_slot, true);

    error = sel4utils_spawn_process_v(&proc, &env->vka, &env->vspace,
                                      argc, argv, 1);
    test_error_eq(error, 0);
    // seL4_DebugNameThread(proc.thread.tcb.cptr, "bench");
    // seL4_DebugDumpScheduler();
    return 0;
}

static int spawn_pd_osm(env_t env, vka_object_t **ret_ep)
{
    int error;
    seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
    seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);
    seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);

    seL4_CPtr slot;
    error = vka_cspace_alloc(&env->vka, &slot);
    assert(error == 0);

    pd_client_context_t pd_os_cap;
    error = pd_component_client_connect(pd_rde, slot, &pd_os_cap);
    assert(error == 0);

    /* Create a new ADS Cap, which will be in the context of a PD and image */
    error = vka_cspace_alloc(&env->vka, &slot);
    assert(error == 0);

    ads_client_context_t ads_os_cap;
    error = ads_component_client_connect(ads_rde, slot, &ads_os_cap);
    assert(error == 0);

    error = vka_cspace_alloc(&env->vka, &slot);
    assert(error == 0);

    cpu_client_context_t cpu_os_cap;
    error = cpu_component_client_connect(cpu_rde, slot, &cpu_os_cap);
    assert(error == 0);

    // Make a new AS, loads an image
    error = pd_client_load(&pd_os_cap, &ads_os_cap, &cpu_os_cap, "hello_benchmark");
    assert(error == 0);
    printf("Loaded hello\n");

    vka_object_t *ep = malloc(sizeof(vka_object_t));
    error = vka_alloc_endpoint(&env->vka, ep);
    test_error_eq(error, 0);
    *ret_ep = ep;

    error = pd_client_send_cap(&pd_os_cap, ep->cptr, &slot);
    test_error_eq(error, 0);
    // Start the CPU.
    seL4_Word args[2] = {slot, 0};
    error = pd_client_start(&pd_os_cap,
                            2,
                            args); // with this arg.
    test_error_eq(error, 0);

    return 0;
}

static int benchmark_pd_create(env_t env, bool native, vka_object_t **ret_ep)
{
    int error;
    ccnt_t pd_create_start_time;
    SEL4BENCH_READ_CCNT(pd_create_start_time);

    if (native)
    {
        error = spawn_pd_native(env, ret_ep);
    }
    else
    {
        error = spawn_pd_osm(env, ret_ep);
    }
    test_error_eq(error, 0);

    seL4_MessageInfo_t tag = seL4_Recv((*ret_ep)->cptr, NULL);
    seL4_Word bench_type = seL4_GetMR(0);
    test_assert(bench_type == BM_PD_CREATE);
    seL4_Word pd_create_end_time = seL4_GetMR(1);
    TEST_LOG("%s PD CREATE TIME: %ld (start: %ld, end: %ld)", get_bench_type_name(native), pd_create_end_time - pd_create_start_time, pd_create_start_time, pd_create_end_time);
    return error;
}

static int benchmark_ipc(vka_object_t *ep, bool native)
{
    ccnt_t ipc_recv_end;
    seL4_MessageInfo_t tag = seL4_Recv(ep->cptr, NULL);
    SEL4BENCH_READ_CCNT(ipc_recv_end);
    TEST_LOG("%s: IPC Recv'd %ld", get_bench_type_name(native), ipc_recv_end);
    seL4_Word bench_type = seL4_GetMR(0);
    assert(bench_type == BM_IPC);
    seL4_Word pd_ipc_start_time = seL4_GetMR(1);

    tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, BM_IPC);
    seL4_SetMR(1, ipc_recv_end);
    seL4_Reply(tag);

    return 0;
}

int bench_native(env_t env)
{
    sel4bench_init();

    vka_object_t *ep;
    benchmark_pd_create(env, true, &ep);

    benchmark_ipc(ep, true);

    sel4bench_destroy();
    free(ep);
    return sel4test_get_result();
}

int bench_osm(env_t env)
{
    sel4bench_init();

    vka_object_t *ep;
    benchmark_pd_create(env, false, &ep);

    benchmark_ipc(ep, false);

    sel4bench_destroy();
    free(ep);
    return sel4test_get_result();
}

DEFINE_TEST(GPIBM001,
            "Native seL4 Benchmarks",
            bench_native,
            true)

DEFINE_TEST(GPIBM002,
            "CellulOS Benchmarks",
            bench_osm,
            false)