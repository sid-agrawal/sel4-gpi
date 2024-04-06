/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sel4/sel4.h>
#include <sel4test/test.h>
#include <vka/capops.h>
#include <sel4gpi/debug.h>
#include "../test.h"
#include "../helpers.h"
#include <stdio.h>
#include <fcntl.h>

#include <sel4gpi/pd_clientapi.h>
#include <sel4bench/arch/sel4bench.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/bench_utils.h>
#include <ramdisk_client.h>
#include <fs_client.h>

#define TEST_LOG(msg, ...)                                  \
    do                                                      \
    {                                                       \
        printf("%s(): " msg "\n", __func__, ##__VA_ARGS__); \
    } while (0)
#define NUM_RUNS 100

static vka_object_t hello_ep;
static sel4utils_process_t native_proc;
static vka_object_t bench_frames[NUM_RUNS];
static vka_object_t bench_tcbs[NUM_RUNS];
static vka_object_t bench_cnodes[NUM_RUNS];
static vka_object_t bench_vspaces[NUM_RUNS];

static pd_client_context_t bench_pds[NUM_RUNS];
static mo_client_context_t bench_mos[NUM_RUNS];
static cpu_client_context_t bench_cpus[NUM_RUNS];
static ads_client_context_t bench_ads[NUM_RUNS];
static seL4_Word osm_pd_ads_ns_id;

static int bench_frame_i = 0;
static int bench_tcbs_i = 0;
static int bench_cnodes_i = 0;
static int bench_vspace_i = 0;
static int file_i = 0;

static int benchmark_pd_create(env_t env, bool native, uint64_t *time)
{
    ccnt_t pd_create_start;
    ccnt_t pd_create_end;
    int error;

    if (native)
    {
        size_t size_bits = 5;
        SEL4BENCH_READ_CCNT(pd_create_start);
        error = vka_alloc_cnode_object(&env->vka, size_bits, &bench_cnodes[bench_cnodes_i]);
        test_error_eq(error, 0);

        cspacepath_t src;
        vka_cspace_make_path(&env->vka, bench_cnodes[bench_cnodes_i].cptr, &src);

        seL4_Word cspace_root_data = api_make_guard_skip_word(seL4_WordBits - size_bits);
        cspacepath_t dest = {.capPtr = 1, .root = bench_cnodes[bench_cnodes_i].cptr, .capDepth = size_bits};
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
        error = pd_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_PD), slot, &bench_pds[bench_cnodes_i]);
        test_error_eq(error, 0);
        SEL4BENCH_READ_CCNT(pd_create_end);
    }
    // TEST_LOG("%s: PD CREATE: %ld", get_bench_type_name(true), pd_create_end - pd_create_start);
    // vka_free_object(&env->vka, &cnode);
    bench_cnodes_i++;
    *time = pd_create_end - pd_create_start;
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

    seL4_Word argc = 3;
    char string_args[argc][WORD_STRING_SIZE];
    char *argv[argc];
    sel4utils_create_word_args(string_args, argv, argc, ep_slot, true, false);

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
    // seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
    // seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);
    // seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);
    // ccnt_t pd_create_start;
    // ccnt_t pd_create_end;

    seL4_CPtr slot;
    // error = vka_cspace_alloc(&env->vka, &slot);
    // test_error_eq(error, 0);

    // SEL4BENCH_READ_CCNT(pd_create_start);
    // error = pd_component_client_connect(pd_rde, slot, &osm_pd);
    // test_error_eq(error, 0);
    // SEL4BENCH_READ_CCNT(pd_create_end);

    // TEST_LOG("%s: PD CREATE: %ld\n", get_bench_type_name(false), pd_create_end - pd_create_start);

    // /* Create a new ADS Cap, which will be in the context of a PD and image */
    // error = vka_cspace_alloc(&env->vka, &slot);
    // test_error_eq(error, 0);

    // error = ads_component_client_connect(ads_rde, slot, &osm_pd_ads, &osm_pd_ads_ns_id);
    // test_error_eq(error, 0);

    // error = vka_cspace_alloc(&env->vka, &slot);
    // test_error_eq(error, 0);

    // cpu_client_context_t cpu_os_cap;
    // error = cpu_component_client_connect(cpu_rde, slot, &cpu_os_cap);
    // test_error_eq(error, 0);

    // Make a new AS, loads an image
    error = pd_client_load(&bench_pds[bench_cnodes_i], &bench_ads[bench_vspace_i], &bench_cpus[bench_tcbs_i], "hello_benchmark");
    test_error_eq(error, 0);
    printf("Loaded hello\n");

    error = pd_client_send_cap(&bench_pds[bench_cnodes_i], hello_ep.cptr, &slot);
    test_error_eq(error, 0);
    // Start the CPU.
    seL4_Word args[3] = {slot, 0, 0};
    error = pd_client_start(&bench_pds[bench_cnodes_i],
                            3,
                            args); // with this arg.
    test_error_eq(error, 0);

    bench_cnodes_i++;
    bench_vspace_i++;
    bench_tcbs_i++;

    return 0;
}

static int benchmark_pd_spawn(env_t env, bool native, uint64_t *time)
{
    int error;
    ccnt_t pd_spawn_start;

    SEL4BENCH_READ_CCNT(pd_spawn_start);

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
    seL4_Word pd_spawn_end = seL4_GetMR(1);
    // TEST_LOG("%s PD SPAWN: %ld", get_bench_type_name(native), pd_spawn_end - pd_spawn_start);
    *time = pd_spawn_end - pd_spawn_start;
    return error;
}

static int benchmark_ipc(env_t env, bool native, uint64_t *time)
{
    // ccnt_t ipc_recv_end;
    // seL4_MessageInfo_t tag = seL4_Recv(hello_ep.cptr, NULL);
    // SEL4BENCH_READ_CCNT(ipc_recv_end);
    // seL4_Word bench_type = seL4_GetMR(0);
    // test_assert(bench_type == BM_IPC);
    // seL4_Word pd_ipc_start_time = seL4_GetMR(1);

    // tag = seL4_MessageInfo_new(0, 0, 0, 2);
    // seL4_SetMR(0, BM_IPC);
    // seL4_SetMR(1, ipc_recv_end);
    // seL4_Reply(tag);

    ccnt_t ipc_start;
    ccnt_t ipc_end;
    SEL4BENCH_READ_CCNT(ipc_start);
    pd_client_context_t self_pd_conn = {.badged_server_ep_cspath.capPtr = sel4gpi_get_pd_cap()};
    pd_client_bench_ipc(&self_pd_conn);
    SEL4BENCH_READ_CCNT(ipc_end);
    // TEST_LOG("%s: IPC ROUND TRIP TIME: %ld", get_bench_type_name(native), ipc_end - ipc_start);
    *time = ipc_end - ipc_start;

    return 0;
}

static int benchmark_give_resource(env_t env, bool native, uint64_t *time)
{
    int error;
    ccnt_t give_res_start;
    ccnt_t give_res_end;

    if (native)
    {
        int error = vka_alloc_frame(&env->vka, seL4_PageBits, &bench_frames[bench_frame_i]);
        test_error_eq(error, 0);

        SEL4BENCH_READ_CCNT(give_res_start);
        sel4utils_copy_cap_to_process(&native_proc, &env->vka, bench_frames[bench_frame_i].cptr);
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
                                            &bench_mos[bench_frame_i]);
        test_error_eq(error, 0);

        SEL4BENCH_READ_CCNT(give_res_start);
        seL4_CPtr slot;
        pd_client_send_cap(&bench_pds[0], bench_mos[bench_frame_i].badged_server_ep_cspath.capPtr, &slot);
        SEL4BENCH_READ_CCNT(give_res_end);
    }
    bench_frame_i++;
    // TEST_LOG("%s: PD GIVE RESOURCE TIME: %ld", get_bench_type_name(native), give_res_end - give_res_start);
    *time = give_res_end - give_res_start;
    return error;
}

static int benchmark_ads_create(env_t env, bool native, uint64_t *time)
{
    int error;
    ccnt_t ads_create_start;
    ccnt_t ads_create_end;

    if (native)
    {
        SEL4BENCH_READ_CCNT(ads_create_start);
        error = vka_alloc_vspace_root(&env->vka, &bench_vspaces[bench_vspace_i]);
        test_error_eq(error, 0);

        vspace_t new_vspace;
        sel4utils_alloc_data_t new_vspace_alloc_data;
        error = sel4utils_get_vspace(&env->vspace, &new_vspace, &new_vspace_alloc_data, &env->vka, bench_vspaces[bench_vspace_i].cptr,
                                     sel4utils_allocated_object, (void *)&native_proc);

        test_error_eq(error, 0);
        SEL4BENCH_READ_CCNT(ads_create_end);
        // vka_free_object(&env->vka, &vspace_root);
    }
    else
    {
        seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
        SEL4BENCH_READ_CCNT(ads_create_start);

        seL4_CPtr slot;
        error = vka_cspace_alloc(&env->vka, &slot);
        test_error_eq(error, 0);

        error = ads_component_client_connect(ads_rde, slot, &bench_ads[bench_vspace_i], &osm_pd_ads_ns_id);
        SEL4BENCH_READ_CCNT(ads_create_end);
    }

    bench_vspace_i++;

    // TEST_LOG("%s: ADS CREATE %ld", get_bench_type_name(native), ads_create_end - ads_create_start);
    *time = ads_create_end - ads_create_start;
    return error;
}

static int benchmark_ads_attach(env_t env, bool native, uint64_t *time)
{
    int error;
    ccnt_t ads_attach_start;
    ccnt_t ads_attach_end;
    void *mapped_vaddr;

    SEL4BENCH_READ_CCNT(ads_attach_start);
    if (native)
    {
        mapped_vaddr = sel4utils_map_pages(&native_proc.vspace, &bench_frames[bench_frame_i].cptr, (uintptr_t *)&native_proc.vspace, seL4_AllRights, 1, seL4_PageBits, 1);
        test_assert(mapped_vaddr != NULL);
    }
    else
    {
        ads_client_context_t osm_pd_ads_rde = {.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_ns_id(2, GPICAP_TYPE_ADS)};
        error = ads_client_attach(&osm_pd_ads_rde, NULL, &bench_mos[bench_frame_i], &mapped_vaddr);
        test_error_eq(error, 0);
    }
    bench_frame_i++;
    SEL4BENCH_READ_CCNT(ads_attach_end);
    // TEST_LOG("%s: ADS ATTACH %ld", get_bench_type_name(native), ads_attach_end - ads_attach_start);
    *time = ads_attach_end - ads_attach_start;
    return error;
}

static int benchmark_cpu_create(env_t env, bool native, uint64_t *time_create)
{
    int error;
    ccnt_t cpu_create_start;
    ccnt_t cpu_create_end;

    if (native)
    {
        SEL4BENCH_READ_CCNT(cpu_create_start);
        error = vka_alloc_tcb(&env->vka, &bench_tcbs[bench_tcbs_i]);
        test_error_eq(error, 0);
        SEL4BENCH_READ_CCNT(cpu_create_end);
    }
    else
    {
        seL4_CPtr slot;
        error = vka_cspace_alloc(&env->vka, &slot);
        test_error_eq(error, 0);

        SEL4BENCH_READ_CCNT(cpu_create_start);
        error = cpu_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_CPU), slot, &bench_cpus[bench_tcbs_i]);
        test_error_eq(error, 0);
        SEL4BENCH_READ_CCNT(cpu_create_end);
    }

    bench_tcbs_i++;
    *time_create = cpu_create_end - cpu_create_start;
    return error;
}

static int benchmark_cpu_bind(env_t env, bool native, uint64_t *time)
{
    int error;

    ccnt_t cpu_bind_start;
    ccnt_t cpu_bind_end;

    vka_object_t cspace;
    error = vka_alloc_cnode_object(&env->vka, 5, &cspace);
    test_error_eq(error, 0);

    if (native)
    {
        error = seL4_ARCH_ASIDPool_Assign(env->asid_pool, bench_vspaces[bench_vspace_i].cptr);
        test_error_eq(error, 0);

        SEL4BENCH_READ_CCNT(cpu_bind_start);
        error = seL4_TCB_SetSpace(bench_tcbs[bench_tcbs_i].cptr, seL4_CapNull, cspace.cptr, 0, bench_vspaces[bench_vspace_i].cptr, 0);
        test_error_eq(error, 0);
        SEL4BENCH_READ_CCNT(cpu_bind_end);
    }
    else
    {
        seL4_CPtr slot;
        error = vka_cspace_alloc(&env->vka, &slot);
        test_error_eq(error, 0);

        cpu_client_context_t cpu_conn;
        error = cpu_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_CPU), slot, &cpu_conn);
        test_error_eq(error, 0);

        error = vka_cspace_alloc(&env->vka, &slot);
        test_error_eq(error, 0);
        ads_client_context_t ads_conn;
        error = ads_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_ADS), slot, &ads_conn, NULL);
        test_error_eq(error, 0);

        SEL4BENCH_READ_CCNT(cpu_bind_start);
        error = cpu_client_config(&cpu_conn, &ads_conn, NULL, cspace.cptr, seL4_CapNull, 0, 0);
        test_error_eq(error, 0);
        SEL4BENCH_READ_CCNT(cpu_bind_end);
    }

    bench_vspace_i++;
    bench_tcbs_i++;
    // TEST_LOG("%s: CPU CREATE: %ld", get_bench_type_name(native), cpu_create_end - cpu_create_start);
    // TEST_LOG("%s: CPU BIND %ld", get_bench_type_name(native), cpu_bind_end - cpu_bind_start);

    *time = cpu_bind_end - cpu_bind_start;

    return error;
}

static int benchmark_fs_setup(env_t env)
{
    int error;
    /* Initialize the ADS */
    ads_client_context_t ads_conn;
    vka_cspace_make_path(&env->vka, sel4gpi_get_rde_by_ns_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_ADS), &ads_conn.badged_server_ep_cspath);

    /* Initialize the PD */
    pd_client_context_t pd_conn;
    vka_cspace_make_path(&env->vka, sel4gpi_get_pd_cap(), &pd_conn.badged_server_ep_cspath);

    /* Start ramdisk server process */
    uint64_t ramdisk_id;
    seL4_CPtr ramdisk_pd_cap;
    error = start_ramdisk_pd(&ramdisk_pd_cap, &ramdisk_id);
    test_assert(error == 0);

    /* Start fs server process */
    uint64_t fs_id;
    seL4_CPtr fs_pd_cap;
    error = start_xv6fs_pd(ramdisk_id, ramdisk_pd_cap, &fs_pd_cap, &fs_id);
    test_assert(error == 0);

    // Add FS ep to RDE
    error = pd_client_add_rde(&pd_conn, fs_pd_cap, fs_id, NSID_DEFAULT);
    test_assert(error == 0);
    seL4_CPtr fs_client_ep = sel4gpi_get_rde(GPICAP_TYPE_FILE);

    // The libc fs ops should go to the xv6fs server
    xv6fs_client_init();

    return 0;
}

static int benchmark_file_create(env_t env, bool native, uint64_t *time)
{
    // Test file open/write
    ccnt_t file_create_start;
    ccnt_t file_create_end;
    SEL4BENCH_READ_CCNT(file_create_start);
    char fname[20];
    snprintf(fname, 20, "somefile%d", file_i);
    int f = open(fname, O_CREAT | O_RDWR);
    test_assert(f > 0);
    SEL4BENCH_READ_CCNT(file_create_end);

    // TEST_LOG("%s: FILE CREATE: %ld", get_bench_type_name(false), file_create_end - file_create_start);
    *time = file_create_end - file_create_start;
    file_i++;
    printf("%s\n", fname);
    return 0;
}

static void run_bench(env_t env, bool native, int (*bench_fn)(env_t, bool, uint64_t *))
{
    uint64_t time_i;
    uint64_t time_total = 0;
    for (int i = 0; i < NUM_RUNS; i++)
    {
        bench_fn(env, native, &time_i);
        time_total += time_i;
    }
    printf("%ld\n", time_total / NUM_RUNS);
}

int bench_native(env_t env)
{
    sel4bench_init();

    int error = vka_alloc_endpoint(&env->vka, &hello_ep);
    test_error_eq(error, 0);

    run_bench(env, true, benchmark_pd_spawn);
    run_bench(env, true, benchmark_pd_create);
    bench_cnodes_i = 0;
    run_bench(env, true, benchmark_ipc);
    run_bench(env, true, benchmark_give_resource);
    bench_frame_i = 0;
    run_bench(env, true, benchmark_ads_create);
    bench_vspace_i = 0;
    run_bench(env, true, benchmark_ads_attach);
    run_bench(env, true, benchmark_cpu_create);
    bench_tcbs_i = 0;
    run_bench(env, true, benchmark_cpu_bind);

    // // this is so we don't clobber the prints
    // seL4_Recv(hello_ep.cptr, NULL);
    // test_assert(seL4_GetMR(0) == BM_PRINT);

    // // this is so we don't tear down the test before the printing can even happen
    // seL4_Recv(hello_ep.cptr, NULL);
    // test_assert(seL4_GetMR(0) == BM_DONE);

    sel4bench_destroy();
    return sel4test_get_result();
}

int bench_osm(env_t env)
{
    sel4bench_init();
    int error;

    error = benchmark_fs_setup(env);
    test_error_eq(error, 0);

    uint64_t time_i;
    uint64_t time_total = 0;
    for (int i = 0; i < 25; i++)
    {
        benchmark_file_create(env, false, &time_i);
        time_total += time_i;
    }
    printf("%ld\n", time_total / 25);

    error = vka_alloc_endpoint(&env->vka, &hello_ep);
    test_error_eq(error, 0);

    run_bench(env, false, benchmark_pd_create);

    run_bench(env, false, benchmark_ads_create);

    run_bench(env, false, benchmark_cpu_create);

    bench_tcbs_i = 0;
    bench_cnodes_i = 0;
    bench_vspace_i = 0;
    run_bench(env, false, benchmark_pd_spawn);

    bench_tcbs_i = 0;
    run_bench(env, false, benchmark_cpu_bind);

    run_bench(env, false, benchmark_give_resource);

    bench_vspace_i = 0;
    bench_frame_i = 0;
    run_bench(env, false, benchmark_ads_attach);

    run_bench(env, false, benchmark_ipc);

    // // this is so we don't clobber the prints
    // seL4_Recv(hello_ep.cptr, NULL);
    // test_assert(seL4_GetMR(0) == BM_PRINT);

    // // this is so we don't tear down the test before the printing can even happen
    // seL4_Recv(hello_ep.cptr, NULL);
    // test_assert(seL4_GetMR(0) == BM_DONE);

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