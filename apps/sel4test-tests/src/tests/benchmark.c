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
#include <sel4gpi/gpi_client.h>
#include <fs_client.h>
#include <ramdisk_client.h>
#include <fcntl.h>

// #define TEST_DEBUG 0

#ifdef TEST_DEBUG
#define TEST_LOG(msg, ...)               \
    do                                   \
    {                                    \
        printf(msg "\n", ##__VA_ARGS__); \
    } while (0)
#else
#define TEST_LOG(msg, ...)
#endif

static vka_object_t hello_ep;
static sel4utils_process_t native_proc;
static vka_object_t bench_frame;
static pd_client_context_t osm_pd;
static mo_client_context_t bench_mo;
static ads_client_context_t osm_pd_ads;
static seL4_Word osm_pd_ads_ns_id;

static int benchmark_pd_create(env_t env, bool native)
{
    ccnt_t pd_create_start;
    ccnt_t pd_create_end;
    int error;
    TEST_LOG("\nPD CREATE");

    if (native)
    {
        size_t size_bits = 17;
        SEL4BENCH_READ_CCNT(pd_create_start);
        vka_object_t cnode;
        error = vka_alloc_cnode_object(&env->vka, size_bits, &cnode);
        test_error_eq(error, 0);

        cspacepath_t src;
        vka_cspace_make_path(&env->vka, cnode.cptr, &src);

        seL4_Word cspace_root_data = api_make_guard_skip_word(seL4_WordBits - size_bits);
        cspacepath_t dest = {.capPtr = 1, .root = cnode.cptr, .capDepth = size_bits};
        error = vka_cnode_mint(&dest, &src, seL4_AllRights, cspace_root_data);
        test_error_eq(error, 0);
        SEL4BENCH_READ_CCNT(pd_create_end);
    }
    else
    {
        seL4_CPtr slot;
        error = vka_cspace_alloc(&env->vka, &slot);
        test_error_eq(error, 0);

        SEL4BENCH_READ_CCNT(pd_create_start);
        pd_client_context_t pd_conn;
        error = pd_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_PD), slot, &pd_conn);
        test_error_eq(error, 0);
        SEL4BENCH_READ_CCNT(pd_create_end);
    }
    // TEST_LOG("\n%s: PD CREATE: %ld", get_bench_type_name(true), pd_create_end - pd_create_start);
    printf("%ld,", pd_create_end - pd_create_start);
    return error;
}

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

#if CONFIG_MAX_NUM_NODES > 1
    seL4_TCB_GetAffinity_t affinity = seL4_TCB_GetAffinity(native_proc.thread.tcb.cptr);
    TEST_LOG("\naffinity: %ld", affinity.affinity);
#endif
    // seL4_DebugNameThread(proc.thread.tcb.cptr, "bench");
    // seL4_DebugDumpScheduler();
    return 0;
}

static int spawn_pd_osm(env_t env)
{
    int error;

    pd_client_context_t proc;
    pd_resource_config_t *cfg = sel4gpi_configure_process("hello_benchmark", DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES, &proc);
    test_assert(cfg != NULL);

    error = vka_alloc_endpoint(&env->vka, &hello_ep);
    test_error_eq(error, 0);

    seL4_CPtr slot;
    error = pd_client_send_cap(&osm_pd, hello_ep.cptr, &slot);
    test_error_eq(error, 0);
    // Start the CPU.
    int argc = 2;
    seL4_Word args[argc];
    args[0] = slot;
    args[1] = 0;

    sel4gpi_runnable_t runnable = {.pd = proc};
    error = sel4gpi_start_pd(cfg, &runnable, argc, args);
    test_error_eq(error, 0);

    free(cfg);
    return 0;
}

static int benchmark_pd_spawn(env_t env, bool native)
{
    int error;
    ccnt_t pd_create_start_time;
    TEST_LOG("\nPD SPAWN");
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
    printf("%ld,", pd_create_end_time - pd_create_start_time);
    return error;
}

static int benchmark_ipc_rt(env_t env, bool cap_transfer, bool print)
{
    TEST_LOG("\nIPC - %s", cap_transfer ? "CAP TRANSFER" : "NO CAP TRANSFER");
    ccnt_t ipc_start;
    ccnt_t ipc_end;
    seL4_CPtr slot_send;
    seL4_CPtr slot_recv;
    int error;
    // for (int i = 0; i < 10; i++)
    // {
    error = vka_cspace_alloc(&env->vka, &slot_send);
    test_error_eq(error, 0);

    SEL4BENCH_READ_CCNT(ipc_start);
    pd_client_context_t self_pd_conn = sel4gpi_get_pd_conn();
    pd_client_bench_ipc(&self_pd_conn, slot_send, slot_recv, cap_transfer);
    SEL4BENCH_READ_CCNT(ipc_end);
    if (print)
    {
        printf("%ld,", ipc_end - ipc_start);
    }
    // }

    // TEST_LOG("\n%s: IPC ROUND TRIP TIME: %ld", get_bench_type_name(native), ipc_end - ipc_start);
}

static int benchmark_ipc_pd(bool native)
{
    ccnt_t ipc_recv_end;
    seL4_MessageInfo_t tag = seL4_Recv(hello_ep.cptr, NULL);
    SEL4BENCH_READ_CCNT(ipc_recv_end);
    // TEST_LOG("\n%s: IPC Recv'd %ld", get_bench_type_name(native), ipc_recv_end);
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
    TEST_LOG("\nGIVE RESOURCE");
    if (native)
    {
        // SEL4BENCH_READ_CCNT(give_res_start);
        int error = vka_alloc_frame(&env->vka, seL4_PageBits, &bench_frame);
        test_error_eq(error, 0);
        // SEL4BENCH_READ_CCNT(give_res_end);
        // printf("alloc frame: %ld\n", give_res_end - give_res_start);

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
    printf("%ld,", give_res_end - give_res_start);

    return error;
}

static int benchmark_ads_create(env_t env, bool native)
{
    int error;
    ccnt_t ads_create_start;
    ccnt_t ads_create_end;

    TEST_LOG("\nADS CREATE");
    if (native)
    {
        // ccnt_t start;
        // ccnt_t end;
        SEL4BENCH_READ_CCNT(ads_create_start);
        // SEL4BENCH_READ_CCNT(start);
        vka_object_t vspace_root;
        error = vka_alloc_vspace_root(&env->vka, &vspace_root);
        test_error_eq(error, 0);
        // SEL4BENCH_READ_CCNT(end);
        // printf("vka alloc root: %ld\n", end - start);

        // SEL4BENCH_READ_CCNT(start);
        error = assign_asid_pool(env->asid_pool, vspace_root.cptr);
        test_error_eq(error, 0);
        // SEL4BENCH_READ_CCNT(end);
        // printf("asid pool: %ld\n", end - start);

        // SEL4BENCH_READ_CCNT(start);
        vspace_t new_vspace;
        sel4utils_alloc_data_t new_vspace_alloc_data;
        error = sel4utils_get_vspace(&env->vspace, &new_vspace, &new_vspace_alloc_data, &env->vka, vspace_root.cptr,
                                     sel4utils_allocated_object, (void *)&native_proc);

        test_error_eq(error, 0);
        // SEL4BENCH_READ_CCNT(end);
        // printf("sel4utils get vspace: %ld\n", end - start);
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
        error = ads_component_client_connect(ads_rde, slot, &new_ads);
        SEL4BENCH_READ_CCNT(ads_create_end);
    }

    printf("%ld,", ads_create_end - ads_create_start);

    return error;
}

static int benchmark_ads_attach(env_t env, bool native)
{
    int error;
    ccnt_t ads_attach_start;
    ccnt_t ads_attach_end;
    void *mapped_vaddr;
    TEST_LOG("\nADS ATTACH");
    SEL4BENCH_READ_CCNT(ads_attach_start);
    if (native)
    {
        seL4_CPtr cap_in_proc = sel4utils_copy_cap_to_process(&native_proc, &env->vka, bench_frame.cptr);
        reservation_t res = sel4utils_reserve_range_aligned(&env->vspace, PAGE_SIZE_4K, seL4_PageBits, seL4_AllRights, 1, &mapped_vaddr);
        test_assert(mapped_vaddr != NULL);
        // mapped_vaddr = sel4utils_map_pages(&native_proc.vspace, &bench_frame.cptr, (uintptr_t *)&native_proc.vspace, seL4_AllRights, 1, seL4_PageBits, 1);
        error = sel4utils_map_pages_at_vaddr(&env->vspace, &bench_frame.cptr, NULL, mapped_vaddr, 1, seL4_PageBits, res);
        test_error_eq(error, 0);
    }
    else
    {
        ads_client_context_t osm_pd_ads_rde = {.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_space_id(osm_pd_ads_ns_id, GPICAP_TYPE_VMR)};
        error = ads_client_attach(&osm_pd_ads_rde, NULL, &bench_mo, SEL4UTILS_RES_TYPE_GENERIC, &mapped_vaddr);
        test_error_eq(error, 0);
    }

    SEL4BENCH_READ_CCNT(ads_attach_end);
    printf("%ld,", ads_attach_end - ads_attach_start);

    return error;
}

static int benchmark_cpu(env_t env, bool native)
{
    int error;
    ccnt_t cpu_create_start;
    ccnt_t cpu_create_end;
    ccnt_t cpu_bind_start;
    ccnt_t cpu_bind_end;
    TEST_LOG("\nCPU");
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
        error = seL4_TCB_Configure(tcb.cptr, seL4_CapNull, cspace.cptr, 0, vspace_root.cptr, 0, 0, seL4_CapNull);
        // error = seL4_TCB_SetSpace(tcb.cptr, seL4_CapNull, cspace.cptr, 0, vspace_root.cptr, 0);
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
        error = ads_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_ADS), slot, &new_ads);
        test_error_eq(error, 0);

        SEL4BENCH_READ_CCNT(cpu_bind_start);
        error = cpu_client_config(&new_cpu, &new_ads, NULL, NULL, 0, seL4_CapNull, 0);
        test_error_eq(error, 0);
        SEL4BENCH_READ_CCNT(cpu_bind_end);
    }

    printf("%ld,", cpu_create_end - cpu_create_start);
    printf("%ld,", cpu_bind_end - cpu_bind_start);

    vka_free_object(&env->vka, &cspace);

    return error;
}

static int benchmark_fs(env_t env)
{
    int error;

    /* Initialize the PD */
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();

    /* Create a memory object for the RR dump */
    seL4_CPtr slot;
    vka_cspace_alloc(&env->vka, &slot);

    /* Start ramdisk server process */
    uint64_t ramdisk_id;
    seL4_CPtr ramdisk_pd_cap;
    error = start_ramdisk_pd(&ramdisk_pd_cap, &ramdisk_id);
    test_assert(error == 0);

    /* Start fs server process */
    uint64_t fs_id;
    seL4_CPtr fs_pd_cap;
    error = start_xv6fs_pd(ramdisk_id, &fs_pd_cap, &fs_id);
    test_assert(error == 0);

    // Add FS ep to RDE
    seL4_CPtr fs_client_ep = sel4gpi_get_rde(GPICAP_TYPE_FILE);

    // The libc fs ops should go to the xv6fs server
    xv6fs_client_init();

    ccnt_t fs_start;
    ccnt_t fs_end;
    // Test file open/write
    SEL4BENCH_READ_CCNT(fs_start);
    int f = open("somename", O_CREAT | O_RDWR);
    test_assert(f > 0);
    SEL4BENCH_READ_CCNT(fs_end);

    printf("%ld,", fs_end - fs_start);
}

int benchmark_mint(env_t env)
{
    int error;
    cspacepath_t src, dest;
    seL4_CPtr slot;
    ccnt_t start;
    ccnt_t end;

    TEST_LOG("CNODE MINT");
    for (int i = 0; i < 30; i++)
    {
        SEL4BENCH_READ_CCNT(start);
        vka_cspace_make_path(&env->vka, hello_ep.cptr, &src);
        test_error_eq(error, 0);

        error = vka_cspace_alloc_path(&env->vka, &dest);
        test_error_eq(error, 0);

        error = vka_cnode_mint(&dest, &src, seL4_AllRights, 1);
        test_error_eq(error, 0);
        SEL4BENCH_READ_CCNT(end);

        printf("%ld\n", end - start);
    }

    return 0;
}

int bench_native(env_t env)
{
    sel4bench_init();
    // printf(",");
    // benchmark_pd_create(env, true); // warmup
    for (int i = 0; i < 5; i++)
    {
        benchmark_ipc_rt(env, true, false);
    }
    benchmark_pd_create(env, true);
    benchmark_ipc_rt(env, true, false);
    benchmark_pd_spawn(env, true);
    benchmark_ipc_rt(env, true, false);
    benchmark_give_resource(env, true);
    benchmark_ipc_rt(env, true, false);
    benchmark_ads_create(env, true);
    benchmark_ipc_rt(env, true, false);
    benchmark_ads_attach(env, true);
    benchmark_ipc_rt(env, true, false);
    benchmark_cpu(env, true);
    benchmark_ipc_rt(env, true, true);
    benchmark_ipc_pd(true);
    printf("\n");

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
    // benchmark_fs(env);
    // benchmark_pd_create(env, false); // warmup
    for (int i = 0; i < 5; i++)
    {
        benchmark_ipc_rt(env, true, false);
    }
    benchmark_pd_create(env, false);
    benchmark_pd_spawn(env, false);
    benchmark_give_resource(env, false);
    benchmark_ads_create(env, false);
    benchmark_ads_attach(env, false);
    benchmark_cpu(env, false);
    benchmark_ipc_rt(env, true, true);
    benchmark_ipc_pd(false);
    printf("\n");
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