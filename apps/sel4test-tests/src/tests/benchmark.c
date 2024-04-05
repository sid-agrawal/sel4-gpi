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

static vka_object_t hello_ep;
static sel4utils_process_t native_proc;
static vka_object_t bench_frame;
static pd_client_context_t osm_pd;
static mo_client_context_t bench_mo;
static ads_client_context_t osm_pd_ads;
static seL4_Word osm_pd_ads_ns_id;

static int spawn_pd_native(env_t env)
{
    int error;
    sel4utils_process_config_t config = process_config_default_simple(&env->simple, "hello_benchmark", env->priority);
    config = process_config_mcp(config, seL4_MaxPrio);
    config = process_config_auth(config, simple_get_tcb(&env->simple));
    config = process_config_create_cnode(config, 12);
    error = sel4utils_configure_process_custom(&native_proc, &env->vka, &env->vspace, config);

    error = vka_alloc_endpoint(&env->vka, &hello_ep);
    test_error_eq(error, 0);

    // cspacepath_t src, dest;
    // vka_cspace_make_path(&env->vka, ep.cptr, &src);
    seL4_CPtr ep_slot = sel4utils_copy_cap_to_process(&native_proc, &env->vka, hello_ep.cptr);

    seL4_Word argc = 2;
    char string_args[argc][WORD_STRING_SIZE];
    char *argv[argc];
    sel4utils_create_word_args(string_args, argv, argc, ep_slot, true);

    error = sel4utils_spawn_process_v(&native_proc, &env->vka, &env->vspace,
                                      argc, argv, 1);
    test_error_eq(error, 0);
    // seL4_DebugNameThread(proc.thread.tcb.cptr, "bench");
    // seL4_DebugDumpScheduler();
    return 0;
}

static int spawn_pd_osm(env_t env)
{
    int error;
    seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
    seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);
    seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);

    seL4_CPtr slot;
    error = vka_cspace_alloc(&env->vka, &slot);
    test_error_eq(error, 0);

    error = pd_component_client_connect(pd_rde, slot, &osm_pd);
    test_error_eq(error, 0);

    /* Create a new ADS Cap, which will be in the context of a PD and image */
    error = vka_cspace_alloc(&env->vka, &slot);
    test_error_eq(error, 0);

    error = ads_component_client_connect(ads_rde, slot, &osm_pd_ads, &osm_pd_ads_ns_id);
    test_error_eq(error, 0);

    error = vka_cspace_alloc(&env->vka, &slot);
    test_error_eq(error, 0);

    cpu_client_context_t cpu_os_cap;
    error = cpu_component_client_connect(cpu_rde, slot, &cpu_os_cap);
    test_error_eq(error, 0);

    // Make a new AS, loads an image
    error = pd_client_load(&osm_pd, &osm_pd_ads, &cpu_os_cap, "hello_benchmark");
    test_error_eq(error, 0);
    printf("Loaded hello\n");

    error = vka_alloc_endpoint(&env->vka, &hello_ep);
    test_error_eq(error, 0);

    error = pd_client_send_cap(&osm_pd, hello_ep.cptr, &slot);
    test_error_eq(error, 0);
    // Start the CPU.
    seL4_Word args[2] = {slot, 0};
    error = pd_client_start(&osm_pd,
                            2,
                            args); // with this arg.
    test_error_eq(error, 0);

    return 0;
}

static int benchmark_pd_create(env_t env, bool native)
{
    int error;
    ccnt_t pd_create_start_time;
    SEL4BENCH_READ_CCNT(pd_create_start_time);

    if (native)
    {
        error = spawn_pd_native(env);
    }
    else
    {
        error = spawn_pd_osm(env);
    }
    test_error_eq(error, 0);

    seL4_MessageInfo_t tag = seL4_Recv(hello_ep.cptr, NULL);
    seL4_Word bench_type = seL4_GetMR(0);
    test_assert(bench_type == BM_PD_CREATE);
    seL4_Word pd_create_end_time = seL4_GetMR(1);
    TEST_LOG("%s PD CREATE TIME: %ld (start: %ld, end: %ld)", get_bench_type_name(native), pd_create_end_time - pd_create_start_time, pd_create_start_time, pd_create_end_time);
    return error;
}

static int benchmark_ipc(bool native)
{
    ccnt_t ipc_recv_end;
    seL4_MessageInfo_t tag = seL4_Recv(hello_ep.cptr, NULL);
    SEL4BENCH_READ_CCNT(ipc_recv_end);
    TEST_LOG("%s: IPC Recv'd %ld", get_bench_type_name(native), ipc_recv_end);
    seL4_Word bench_type = seL4_GetMR(0);
    test_assert(bench_type == BM_IPC);
    seL4_Word pd_ipc_start_time = seL4_GetMR(1);

    tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, BM_IPC);
    seL4_SetMR(1, ipc_recv_end);
    seL4_Reply(tag);

    return 0;
}

static int benchmark_give_resource(env_t env, bool native)
{
    int error;
    ccnt_t give_res_start;
    ccnt_t give_res_end;

    if (native)
    {
        int error = vka_alloc_frame(&env->vka, seL4_PageBits, &bench_frame);
        test_error_eq(error, 0);

        SEL4BENCH_READ_CCNT(give_res_start);
        sel4utils_copy_cap_to_process(&native_proc, &env->vka, bench_frame.cptr);
        SEL4BENCH_READ_CCNT(give_res_end);
    }
    else
    {
        seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);
        cspacepath_t mo_cap_path;
        error = vka_cspace_alloc_path(&env->vka, &mo_cap_path);
        test_error_eq(error, 0);

        error = mo_component_client_connect(mo_rde,
                                            mo_cap_path.capPtr,
                                            1,
                                            &bench_mo);
        test_error_eq(error, 0);

        SEL4BENCH_READ_CCNT(give_res_start);
        seL4_CPtr slot;
        pd_client_send_cap(&osm_pd, bench_mo.badged_server_ep_cspath.capPtr, &slot);
        SEL4BENCH_READ_CCNT(give_res_end);
    }
    TEST_LOG("%s: PD GIVE RESOURCE TIME: %ld", get_bench_type_name(native), give_res_end - give_res_start);

    return error;
}

static int benchmark_ads_create(env_t env, bool native)
{
    int error;
    ccnt_t ads_create_start;
    ccnt_t ads_create_end;

    if (native)
    {
        SEL4BENCH_READ_CCNT(ads_create_start);
        vka_object_t vspace_root;
        error = vka_alloc_vspace_root(&env->vka, &vspace_root);
        test_error_eq(error, 0);

        vspace_t new_vspace;
        sel4utils_alloc_data_t new_vspace_alloc_data;
        error = sel4utils_get_vspace(&env->vspace, &new_vspace, &new_vspace_alloc_data, &env->vka, vspace_root.cptr,
                                     sel4utils_allocated_object, (void *)&native_proc);

        test_error_eq(error, 0);
        SEL4BENCH_READ_CCNT(ads_create_end);
        vka_free_object(&env->vka, &vspace_root);
    }
    else
    {
        seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
        SEL4BENCH_READ_CCNT(ads_create_start);

        seL4_CPtr slot;
        error = vka_cspace_alloc(&env->vka, &slot);
        test_error_eq(error, 0);

        ads_client_context_t new_ads;
        error = ads_component_client_connect(ads_rde, slot, &new_ads, NULL);
        SEL4BENCH_READ_CCNT(ads_create_end);
    }

    TEST_LOG("%s: ADS CREATE %ld", get_bench_type_name(native), ads_create_end - ads_create_start);

    return error;
}

static int benchmark_ads_attach(env_t env, bool native)
{
    int error;
    ccnt_t ads_attach_start;
    ccnt_t ads_attach_end;
    void *mapped_vaddr;

    SEL4BENCH_READ_CCNT(ads_attach_start);
    if (native)
    {
        mapped_vaddr = sel4utils_map_pages(&native_proc.vspace, &bench_frame.cptr, (uintptr_t *)&native_proc.vspace, seL4_AllRights, 1, seL4_PageBits, 1);
        test_assert(mapped_vaddr != NULL);
    }
    else
    {
        ads_client_context_t osm_pd_ads_rde = {.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_ns_id(osm_pd_ads_ns_id, GPICAP_TYPE_ADS)};
        error = ads_client_attach(&osm_pd_ads_rde, NULL, &bench_mo, &mapped_vaddr);
        test_error_eq(error, 0);
    }

    SEL4BENCH_READ_CCNT(ads_attach_end);
    TEST_LOG("%s: ADS ATTACH %ld", get_bench_type_name(native), ads_attach_end - ads_attach_start);

    return error;
}

static int benchmark_cpu(env_t env, bool native)
{
    int error;
    ccnt_t cpu_create_start;
    ccnt_t cpu_create_end;
    ccnt_t cpu_bind_start;
    ccnt_t cpu_bind_end;

    vka_object_t cspace;
    error = vka_alloc_cnode_object(&env->vka, 5, &cspace);
    test_error_eq(error, 0);

    if (native)
    {
        vka_object_t tcb;
        SEL4BENCH_READ_CCNT(cpu_create_start);
        error = vka_alloc_tcb(&env->vka, &tcb);
        test_error_eq(error, 0);
        SEL4BENCH_READ_CCNT(cpu_create_end);

        vka_object_t vspace_root;
        error = vka_alloc_vspace_root(&env->vka, &vspace_root);
        test_error_eq(error, 0);

        error = seL4_ARCH_ASIDPool_Assign(env->asid_pool, vspace_root.cptr);
        test_error_eq(error, 0);

        SEL4BENCH_READ_CCNT(cpu_bind_start);
        error = seL4_TCB_SetSpace(tcb.cptr, seL4_CapNull, cspace.cptr, 0, vspace_root.cptr, 0);
        test_error_eq(error, 0);
        SEL4BENCH_READ_CCNT(cpu_bind_end);

        vka_free_object(&env->vka, &vspace_root); // unassign from ASID pool?
        vka_free_object(&env->vka, &tcb);
    }
    else
    {
        seL4_CPtr slot;
        error = vka_cspace_alloc(&env->vka, &slot);
        test_error_eq(error, 0);

        SEL4BENCH_READ_CCNT(cpu_create_start);
        cpu_client_context_t new_cpu;
        error = cpu_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_CPU), slot, &new_cpu);
        test_error_eq(error, 0);
        SEL4BENCH_READ_CCNT(cpu_create_end);

        ads_client_context_t new_ads;
        error = vka_cspace_alloc(&env->vka, &slot);
        test_error_eq(error, 0);
        error = ads_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_ADS), slot, &new_ads, NULL);
        test_error_eq(error, 0);

        SEL4BENCH_READ_CCNT(cpu_bind_start);
        error = cpu_client_config(&new_cpu, &new_ads, NULL, cspace.cptr, seL4_CapNull, 0, 0);
        test_error_eq(error, 0);
        SEL4BENCH_READ_CCNT(cpu_bind_end);
    }

    TEST_LOG("%s: CPU CREATE: %ld", get_bench_type_name(native), cpu_create_end - cpu_create_start);
    TEST_LOG("%s: CPU BIND %ld", get_bench_type_name(native), cpu_bind_end - cpu_bind_start);

    vka_free_object(&env->vka, &cspace);

    return error;
}

int bench_native(env_t env)
{
    sel4bench_init();
    benchmark_pd_create(env, true);
    benchmark_ipc(true);
    benchmark_give_resource(env, true);
    benchmark_ads_create(env, true);
    benchmark_ads_attach(env, true);
    benchmark_cpu(env, true);

    // this is so we don't clobber the prints
    seL4_Recv(hello_ep.cptr, NULL);
    test_assert(seL4_GetMR(0) == BM_PRINT);

    // this is so we don't tear down the test before the printing can even happen
    seL4_Recv(hello_ep.cptr, NULL);
    test_assert(seL4_GetMR(0) == BM_DONE);

    vka_free_object(&env->vka, &bench_frame);
    vka_free_object(&env->vka, &hello_ep);
    sel4bench_destroy();
    return sel4test_get_result();
}

int bench_osm(env_t env)
{
    sel4bench_init();
    benchmark_pd_create(env, false);
    benchmark_ipc(false);
    benchmark_give_resource(env, false);
    benchmark_ads_create(env, false);
    benchmark_ads_attach(env, false);
    benchmark_cpu(env, false);

    // this is so we don't clobber the prints
    seL4_Recv(hello_ep.cptr, NULL);
    test_assert(seL4_GetMR(0) == BM_PRINT);

    // this is so we don't tear down the test before the printing can even happen
    seL4_Recv(hello_ep.cptr, NULL);
    test_assert(seL4_GetMR(0) == BM_DONE);

    vka_free_object(&env->vka, &hello_ep);
    sel4bench_destroy();
    return sel4test_get_result();
}

DEFINE_TEST(GPIBM001,
            "Native seL4 Benchmarks",
            bench_native,
            true)

DEFINE_TEST(GPIBM002,
            "CellulOS Benchmarks",
            bench_osm,
            true)