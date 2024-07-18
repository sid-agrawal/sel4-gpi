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
#include <sel4gpi/ads_client_context.h>
#include <sel4gpi/mo_client_context.h>
#include <sel4gpi/pd_client_context.h>
#include <sel4gpi/cpu_client_context.h>
#include <sel4gpi/pd_creation.h>
#include <fs_client.h>
#include <ramdisk_client.h>
#include <fcntl.h>

// #define TEST_DEBUG

#ifdef TEST_DEBUG
#define TEST_LOG(msg, ...)               \
    do                                   \
    {                                    \
        printf(msg "\n", ##__VA_ARGS__); \
    } while (0)
#else
#define TEST_LOG(msg, ...)
#endif

static size_t cspace_size_bits = 17;

// (XXX) Arya: Is it ok to include test_error_eq in the timings?

static int benchmark_ipc_rt(env_t env, seL4_CPtr cap, bool print);

/**
 * Initializes sel4bench, and issues some IPC calls for warmup
 */
static void benchmark_init(env_t env)
{
    sel4bench_init();

    // Send a cap we know exists for testing
    // If we send this to the root task, it won't be unwrapped
    seL4_CPtr cap = env->endpoint;

    for (int i = 0; i < 5; i++)
    {
        benchmark_ipc_rt(env, cap, false);
    }
}

/**
 * Send an seL4_Call to root task, optionally including a cap to send
 * This times the round-trip time:
 * - send an IPC to root task (optionally with cap transfer)
 * - receive an IPC response from root task (no cap transfer)
 */
static int benchmark_ipc_rt(env_t env, seL4_CPtr cap, bool print)
{
    TEST_LOG("\nCALL RT - %s", cap != seL4_CapNull ? "CAP TRANSFER" : "NO CAP TRANSFER");
    int error = 0;
    ccnt_t call_start;
    ccnt_t call_end;

    SEL4BENCH_READ_CCNT(call_start);
    pd_client_context_t self_pd_conn = {.ep = env->ipc_bench_ep};
    pd_client_bench_ipc(&self_pd_conn, cap, cap != seL4_CapNull);
    SEL4BENCH_READ_CCNT(call_end);

    if (print)
    {
        printf("%ld,", call_end - call_start);
    }
}

/**
 * Send an seL4_Call to a PD, without a cap
 * This times the round-trip time:
 * - send an IPC to PD
 * - receive an IPC response from PD
 */
static int benchmark_ipc_pd(seL4_CPtr ep)
{
    TEST_LOG("\nCALL PD");
    int error = 0;
    ccnt_t call_start;
    ccnt_t call_end;

    SEL4BENCH_READ_CCNT(call_start);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, BM_IPC);
    tag = seL4_Call(ep, tag);
    SEL4BENCH_READ_CCNT(call_end);

    assert(seL4_GetMR(0) == BM_IPC);

    printf("%ld,", call_end - call_start);

    return 0;
}

static int benchmark_pd_create_sel4utils(env_t env, cspacepath_t *cspace)
{
    ccnt_t pd_create_start;
    ccnt_t pd_create_end;
    int error;
    TEST_LOG("\nPD CREATE");

    // For sel4utils, PD creation is just creating a cspace

    SEL4BENCH_READ_CCNT(pd_create_start);

    vka_object_t cnode;
    cspacepath_t dest;
    seL4_Word cspace_root_data;

    error = vka_alloc_cnode_object(&env->vka, cspace_size_bits, &cnode);
    test_error_eq(error, 0);
    vka_cspace_make_path(&env->vka, cnode.cptr, cspace);
    cspace_root_data = api_make_guard_skip_word(seL4_WordBits - cspace_size_bits);
    dest.capPtr = 1;
    dest.root = cnode.cptr;
    dest.capDepth = cspace_size_bits;
    error = vka_cnode_mint(&dest, cspace, seL4_AllRights, cspace_root_data);
    test_error_eq(error, 0);

    SEL4BENCH_READ_CCNT(pd_create_end);

    printf("%ld,", pd_create_end - pd_create_start);

    return 0;
}

static int benchmark_pd_create_osm(pd_client_context_t *pd)
{
    ccnt_t pd_create_start;
    ccnt_t pd_create_end;
    int error;
    TEST_LOG("\nPD CREATE");

    // Create the data MO first, not part of timing
    mo_client_context_t mo;
    error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO), 1, MO_PAGE_BITS, &mo);
    test_error_eq(error, 0);

    // Get the PD cap
    SEL4BENCH_READ_CCNT(pd_create_start);

    error = pd_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_PD), &mo, pd);
    test_error_eq(error, 0);

    SEL4BENCH_READ_CCNT(pd_create_end);

    printf("%ld,", pd_create_end - pd_create_start);

    return 0;
}

static int benchmark_pd_spawn_sel4utils(env_t env, sel4utils_process_t *sel4utils_proc, seL4_CPtr *ep)
{
    ccnt_t pd_create_start_time;
    TEST_LOG("\nPD SPAWN");
    SEL4BENCH_READ_CCNT(pd_create_start_time);

    // (XXX) Arya: Should we include the ep allocation in timing?
    // I think not, because the EP is not always used for PD spawn,
    // but we use it here just to get the spawn time
    vka_object_t hello_ep;
    int error = vka_alloc_endpoint(&env->vka, &hello_ep);
    test_error_eq(error, 0);
    *ep = hello_ep.cptr;

    SEL4BENCH_READ_CCNT(pd_create_start_time);

    // Configure the process
    sel4utils_process_config_t config = process_config_default_simple(&env->simple, "hello_benchmark", env->priority);
    config = process_config_mcp(config, seL4_MaxPrio);
    config = process_config_auth(config, simple_get_tcb(&env->simple));
    config = process_config_create_cnode(config, 12);
    error = sel4utils_configure_process_custom(sel4utils_proc, &env->vka, &env->vspace, config);
    test_error_eq(error, 0);

    // Copy the parent cap, so the process can respond
    // (XXX) Arya: Should this be subtracted from timing?
    seL4_CPtr ep_slot = sel4utils_copy_cap_to_process(sel4utils_proc, &env->vka, hello_ep.cptr);

    // Prepare the arguments
    seL4_Word argc = 2;
    char string_args[argc][WORD_STRING_SIZE];
    char *argv[argc];
    sel4utils_create_word_args(string_args, argv, argc, ep_slot, true);

    // Spawn
    error = sel4utils_spawn_process_v(sel4utils_proc, &env->vka, &env->vspace,
                                      argc, argv, 1);
    test_error_eq(error, 0);

    // The PD will send the time that it spawned
    seL4_MessageInfo_t tag = seL4_Recv(hello_ep.cptr, NULL);
    seL4_Word bench_type = seL4_GetMR(0);
    test_assert(bench_type == BM_PD_CREATE);
    seL4_Word pd_create_end_time = seL4_GetMR(1);
    printf("%ld,", pd_create_end_time - pd_create_start_time);
    return error;

#if CONFIG_MAX_NUM_NODES > 1
    seL4_TCB_GetAffinity_t affinity = seL4_TCB_GetAffinity(sel4utils_proc.thread.tcb.cptr);
    TEST_LOG("\naffinity: %ld", affinity.affinity);
#endif
    // seL4_DebugNameThread(proc.thread.tcb.cptr, "bench");
    // seL4_DebugDumpScheduler();
    return 0;
}

// This tests specifically a process PD, to compare with sel4test version
static int benchmark_pd_spawn_osm(pd_client_context_t *osm_pd, seL4_CPtr *ep)
{
    ccnt_t pd_create_start_time;
    TEST_LOG("\nPD SPAWN");
    SEL4BENCH_READ_CCNT(pd_create_start_time);

    // (XXX) Arya: Don't include EP creation in timing for consistency with spawn_pd_sel4utils
    ep_client_context_t hello_ep;
    int error = ep_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_EP), &hello_ep);
    test_error_eq(error, 0);
    *ep = hello_ep.raw_endpoint;

    SEL4BENCH_READ_CCNT(pd_create_start_time);

    // Configure the PD
    sel4gpi_runnable_t runnable = {0};
    pd_config_t *cfg = sel4gpi_configure_process("hello_benchmark", DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES, &runnable);
    test_assert(cfg != NULL);
    *osm_pd = runnable.pd;

    // Send the parent endpoint to the PD
    // (XXX) Arya: Should this be subtracted from timing?
    seL4_CPtr slot;
    error = pd_client_send_cap(osm_pd, hello_ep.raw_endpoint, &slot);
    test_error_eq(error, 0);

    // Prepare the arguments
    int argc = 2;
    seL4_Word args[argc];
    args[0] = slot;
    args[1] = 0;

    error = sel4gpi_prepare_pd(cfg, &runnable, argc, args);
    test_error_eq(error, 0);

    // Start the PD
    error = sel4gpi_start_pd(&runnable);
    test_error_eq(error, 0);

    // The PD will send the time that it spawned
    seL4_MessageInfo_t tag = seL4_Recv(hello_ep.raw_endpoint, NULL);
    seL4_Word bench_type = seL4_GetMR(0);
    test_assert(bench_type == BM_PD_CREATE);
    seL4_Word pd_create_end_time = seL4_GetMR(1);
    printf("%ld,", pd_create_end_time - pd_create_start_time);
    return error;

    sel4gpi_config_destroy(cfg);
    return 0;
}

static int benchmark_send_cap_sel4utils(env_t env, sel4utils_process_t *sel4utils_proc)
{
    int error;
    ccnt_t send_cap_start;
    ccnt_t send_cap_end;
    TEST_LOG("\nGIVE RESOURCE");

    // Allocate a frame to send
    vka_object_t bench_frame;
    error = vka_alloc_frame(&env->vka, seL4_PageBits, &bench_frame);
    test_error_eq(error, 0);

    // Send the cap
    SEL4BENCH_READ_CCNT(send_cap_start);
    sel4utils_copy_cap_to_process(sel4utils_proc, &env->vka, bench_frame.cptr);
    SEL4BENCH_READ_CCNT(send_cap_end);

    printf("%ld,", send_cap_end - send_cap_start);

    return error;
}

static int benchmark_send_cap_osm(pd_client_context_t *osm_pd)
{
    int error;
    ccnt_t send_cap_start;
    ccnt_t send_cap_end;
    TEST_LOG("\nGIVE RESOURCE");

    // Create an MO to send
    mo_client_context_t mo;
    error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO),
                                        1,
                                        MO_PAGE_BITS,
                                        &mo);
    test_error_eq(error, 0);

    // Send the MO resource
    SEL4BENCH_READ_CCNT(send_cap_start);
    seL4_CPtr slot;
    error = pd_client_send_cap(osm_pd, mo.ep, &slot);
    SEL4BENCH_READ_CCNT(send_cap_end);

    test_error_eq(error, 0);

    printf("%ld,", send_cap_end - send_cap_start);
    return error;
}

static int benchmark_ads_create_sel4utils(env_t env, vspace_t *vspace)
{
    int error;
    ccnt_t ads_create_start;
    ccnt_t ads_create_end;

    TEST_LOG("\nADS CREATE");

    SEL4BENCH_READ_CCNT(ads_create_start);

    sel4utils_alloc_data_t *vspace_alloc_data = calloc(1, sizeof(sel4utils_alloc_data_t));

    vka_object_t vspace_root;
    error = vka_alloc_vspace_root(&env->vka, &vspace_root);
    test_error_eq(error, 0);

    error = assign_asid_pool(env->asid_pool, vspace_root.cptr);
    test_error_eq(error, 0);

    error = sel4utils_get_empty_vspace(&env->vspace, vspace, vspace_alloc_data, &env->vka, vspace_root.cptr,
                                       NULL, NULL);
    test_error_eq(error, 0);
    SEL4BENCH_READ_CCNT(ads_create_end);

    printf("%ld,", ads_create_end - ads_create_start);

    return error;
}

static int benchmark_ads_create_osm(ads_client_context_t *osm_ads)
{
    int error;
    ccnt_t ads_create_start;
    ccnt_t ads_create_end;

    TEST_LOG("\nADS CREATE");

    seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);

    SEL4BENCH_READ_CCNT(ads_create_start);
    error = ads_component_client_connect(ads_rde, osm_ads);
    SEL4BENCH_READ_CCNT(ads_create_end);

    printf("%ld,", ads_create_end - ads_create_start);

    return error;
}

static int benchmark_ads_attach_sel4utils(env_t env, vspace_t *vspace, seL4_CPtr *frame_cap, void **frame_addr)
{
    int error;
    ccnt_t ads_attach_start;
    ccnt_t ads_attach_end;
    void *mapped_vaddr;
    TEST_LOG("\nADS ATTACH");

    // Create a frame to attach
    vka_object_t bench_frame;
    error = vka_alloc_frame(&env->vka, seL4_PageBits, &bench_frame);
    test_error_eq(error, 0);

    // Attach it to the ADS
    SEL4BENCH_READ_CCNT(ads_attach_start);

    // (XXX) Arya: Why was this call here?
    // seL4_CPtr cap_in_proc = sel4utils_copy_cap_to_process(&sel4utils_proc, &env->vka, bench_frame.cptr);

    reservation_t res = sel4utils_reserve_range_aligned(vspace, PAGE_SIZE_4K,
                                                        seL4_PageBits, seL4_AllRights, 1, &mapped_vaddr);
    test_assert(mapped_vaddr != NULL);

    error = sel4utils_map_pages_at_vaddr(vspace, &bench_frame.cptr, NULL, mapped_vaddr, 1, seL4_PageBits, res);

    // mapped_vaddr = vspace_map_pages(vspace, &bench_frame.cptr, NULL, seL4_AllRights, 1, seL4_PageBits, 1);
    test_assert(mapped_vaddr != NULL);

    test_error_eq(error, 0);
    SEL4BENCH_READ_CCNT(ads_attach_end);

    printf("%ld,", ads_attach_end - ads_attach_start);

    *frame_cap = bench_frame.cptr;
    *frame_addr = mapped_vaddr;

    return error;
}

static int benchmark_ads_attach_osm(ads_client_context_t *osm_ads, mo_client_context_t *mo, void **mo_vaddr)
{
    int error;
    ccnt_t ads_attach_start;
    ccnt_t ads_attach_end;
    ads_client_context_t osm_pd_ads_rde = {.ep = sel4gpi_get_rde_by_space_id(osm_ads->id, GPICAP_TYPE_VMR)};

    TEST_LOG("\nADS ATTACH");

    // Create an MO to attach
    error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO),
                                        1,
                                        MO_PAGE_BITS,
                                        mo);
    test_error_eq(error, 0);

    // Attach the MO to the ADS
    SEL4BENCH_READ_CCNT(ads_attach_start);
    error = ads_client_attach(&osm_pd_ads_rde, NULL, mo, SEL4UTILS_RES_TYPE_GENERIC, mo_vaddr);
    test_error_eq(error, 0);
    SEL4BENCH_READ_CCNT(ads_attach_end);

    printf("%ld,", ads_attach_end - ads_attach_start);

    return error;
}

static int benchmark_cpu_create_sel4utils(env_t env, vka_object_t *tcb)
{
    int error;
    ccnt_t cpu_create_start;
    ccnt_t cpu_create_end;

    TEST_LOG("\nCPU CREATE");

    SEL4BENCH_READ_CCNT(cpu_create_start);
    error = vka_alloc_tcb(&env->vka, tcb);
    SEL4BENCH_READ_CCNT(cpu_create_end);

    printf("%ld,", cpu_create_end - cpu_create_start);

    return error;
}

static int benchmark_cpu_create_osm(cpu_client_context_t *osm_cpu)
{
    int error;
    ccnt_t cpu_create_start;
    ccnt_t cpu_create_end;

    TEST_LOG("\nCPU CREATE");

    seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);

    SEL4BENCH_READ_CCNT(cpu_create_start);
    error = cpu_component_client_connect(cpu_rde, osm_cpu);
    SEL4BENCH_READ_CCNT(cpu_create_end);

    printf("%ld,", cpu_create_end - cpu_create_start);

    return error;
}

static int benchmark_cpu_bind_sel4utils(env_t env, vka_object_t *tcb,
                                        seL4_CPtr cspace_root, seL4_CPtr vspace_root,
                                        seL4_CPtr ipc_buf_frame, void *ipc_buf_vaddr)
{
    int error;
    ccnt_t cpu_bind_start;
    ccnt_t cpu_bind_end;

    TEST_LOG("\nCPU BIND");

    // Create the fault ep
    cspacepath_t fault_ep_src, fault_ep_dest;
    vka_object_t fault_ep;
    error = vka_alloc_endpoint(&env->vka, &fault_ep);
    test_error_eq(error, 0);

    vka_cspace_make_path(&env->vka, fault_ep.cptr, &fault_ep_src);
    fault_ep_dest.capPtr = 2;
    fault_ep_dest.root = cspace_root;
    fault_ep_dest.capDepth = cspace_size_bits;
    error = vka_cnode_copy(&fault_ep_dest, &fault_ep_src, seL4_AllRights);
    test_error_eq(error, 0);

    // Bind cpu
    SEL4BENCH_READ_CCNT(cpu_bind_start);
    error = seL4_TCB_Configure(tcb->cptr, fault_ep_dest.capPtr, cspace_root,
                               0, vspace_root, 0, ipc_buf_vaddr, ipc_buf_frame);
    SEL4BENCH_READ_CCNT(cpu_bind_end);
    test_error_eq(error, 0);

    printf("%ld,", cpu_bind_end - cpu_bind_start);

    return error;
}

static int benchmark_cpu_bind_osm(cpu_client_context_t *cpu, ads_client_context_t *ads, pd_client_context_t *pd,
                                  mo_client_context_t *ipc_buf_mo, void *ipc_buf_vaddr)
{
    int error;
    ccnt_t cpu_bind_start;
    ccnt_t cpu_bind_end;

    TEST_LOG("\nCPU BIND");

    // Create fault endpoint
    // (XXX) Arya: Don't include EP creation in timing for consistency with spawn_pd_sel4utils
    ep_client_context_t fault_ep;
    error = ep_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_EP), &fault_ep);
    test_error_eq(error, 0);

    seL4_CPtr fault_ep_in_pd;
    error = pd_client_send_cap(pd, fault_ep.raw_endpoint, &fault_ep_in_pd);
    test_error_eq(error, 0);

    // Bind CPU
    SEL4BENCH_READ_CCNT(cpu_bind_start);
    error = cpu_client_config(cpu, ads, pd, ipc_buf_mo, 0, fault_ep_in_pd, ipc_buf_vaddr);
    test_error_eq(error, 0);
    SEL4BENCH_READ_CCNT(cpu_bind_end);

    printf("%ld,", cpu_bind_end - cpu_bind_start);

    return error;
}

/**
 * Benchmarks the time to perform basic FS operation open
 * This could be easily extended to include read/write/etc.
 *
 * There is no 'sel4test only' version of the FS now, so we will need
 * to calculate the number of IPC calls and subtract it from this time
 */
static int benchmark_fs(env_t env)
{
    int error = 0;
    benchmark_init(env);

    /* Initialize the PD */
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();

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

    // Get FS EP
    seL4_CPtr fs_client_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME));

    // The libc fs ops should go to the xv6fs server
    xv6fs_client_init();

    ccnt_t fs_open_start;
    ccnt_t fs_open_end;

    // Test file open
    TEST_LOG("\nFS OPEN");

    SEL4BENCH_READ_CCNT(fs_open_start);
    int f = open("somename", O_CREAT | O_RDWR);
    test_assert(f > 0);
    SEL4BENCH_READ_CCNT(fs_open_end);

    printf("%ld,", fs_open_end - fs_open_start);

    // Terminate PDs
    pd_client_context_t fs_context = {.ep = fs_pd_cap};
    error = pd_client_terminate(&fs_context);
    test_error_eq(error, 0);

    pd_client_context_t ramdisk_context = {.ep = ramdisk_pd_cap};
    error = pd_client_terminate(&ramdisk_context);
    test_error_eq(error, 0);

    sel4bench_destroy();
    return sel4test_get_result();
}

#if 0
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
#endif

/**
 * Benchmark process spawn
 * + sending cap to the spawned process
 * + sending IPC to the spaned process
 */
int benchmark_pd_spawn_plus_sel4utils(env_t env)
{
    int error = 0;

    benchmark_init(env);

    // Run benchmarks
    sel4utils_process_t sel4utils_proc;
    seL4_CPtr ep;
    error = benchmark_pd_spawn_sel4utils(env, &sel4utils_proc, &ep);
    test_error_eq(error, 0);

    error = benchmark_send_cap_sel4utils(env, &sel4utils_proc);
    test_error_eq(error, 0);

    error = benchmark_ipc_pd(ep);
    test_error_eq(error, 0);

    sel4bench_destroy();
    return sel4test_get_result();
}

/**
 * Benchmark PD spawn (process style)
 * + sending cap to the spawned PD
 * + sending IPC to the spaned PD
 */
int benchmark_pd_spawn_plus_osm(env_t env)
{
    int error = 0;

    benchmark_init(env);

    // Run benchmarks
    pd_client_context_t osm_pd;
    seL4_CPtr ep;
    error = benchmark_pd_spawn_osm(&osm_pd, &ep);
    test_error_eq(error, 0);

    error = benchmark_send_cap_osm(&osm_pd);
    test_error_eq(error, 0);

    error = benchmark_ipc_pd(ep);
    test_error_eq(error, 0);

    // Cleanup
    error = pd_client_terminate(&osm_pd);
    // ignore error, if the PD already terminated

    sel4bench_destroy();
    return sel4test_get_result();
}

/**
 * Benchmark:
 * + cspace create
 * + vspace create
 * + attach a frame to the vspace
 * + cpu create
 * + bind vspace/cspace to cpu
 */
int benchmark_pd_ads_cpu_sel4utils(env_t env)
{
    int error = 0;

    benchmark_init(env);

    // PD create
    cspacepath_t cspace;
    error = benchmark_pd_create_sel4utils(env, &cspace);
    test_error_eq(error, 0);

    // ADS bench
    vspace_t vspace;
    error = benchmark_ads_create_sel4utils(env, &vspace);
    test_error_eq(error, 0);

    seL4_CPtr ipc_frame_cap;
    void *ipc_frame_addr;
    error = benchmark_ads_attach_sel4utils(env, &vspace, &ipc_frame_cap, &ipc_frame_addr);
    test_error_eq(error, 0);

    // CPU bench
    vka_object_t tcb;
    error = benchmark_cpu_create_sel4utils(env, &tcb);
    test_error_eq(error, 0);

    error = benchmark_cpu_bind_sel4utils(env, &tcb, cspace.capPtr, vspace.get_root(&vspace),
                                         ipc_frame_cap, ipc_frame_addr);
    test_error_eq(error, 0);

    // Cleanup cnode
    error = vka_cnode_revoke(&cspace);
    test_error_eq(error, 0);

    sel4bench_destroy();
    return sel4test_get_result();
}

/**
 * Benchmark
 * + PD create
 * + ADS create
 * + attach a MO to the ADS
 * + cpu create
 * + bind ADS/PD cpu
 */
int benchmark_pd_ads_cpu_osm(env_t env)
{
    int error = 0;

    benchmark_init(env);

    // PD create
    pd_client_context_t pd;
    error = benchmark_pd_create_osm(&pd);
    test_error_eq(error, 0);

    // ADS bench
    ads_client_context_t ads;
    error = benchmark_ads_create_osm(&ads);
    test_error_eq(error, 0);

    mo_client_context_t ipc_frame_mo;
    void *ipc_frame_addr;
    error = benchmark_ads_attach_osm(&ads, &ipc_frame_mo, &ipc_frame_addr);
    test_error_eq(error, 0);

    // CPU bench
    cpu_client_context_t cpu;
    error = benchmark_cpu_create_osm(&cpu);
    test_error_eq(error, 0);

    error = benchmark_cpu_bind_osm(&cpu, &ads, &pd, &ipc_frame_mo, ipc_frame_addr);
    test_error_eq(error, 0);

    // Cleanup PD
    error = pd_client_terminate(&pd);
    test_error_eq(error, 0);

    sel4bench_destroy();
    return sel4test_get_result();
}

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM001,
                               "sel4utils PD/ADS/CPU",
                               benchmark_pd_ads_cpu_sel4utils,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM002,
                               "OSM PD/ADS/CPU",
                               benchmark_pd_ads_cpu_osm,
                               OSM,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM003,
                               "sel4utils PD spawn, send cap, and IPC",
                               benchmark_pd_spawn_plus_sel4utils,
                               BASIC,
                               true);

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM004,
                               "osm PD spawn, send cap, and IPC",
                               benchmark_pd_spawn_plus_osm,
                               OSM,
                               true);

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM005,
                               "osm FILE create",
                               benchmark_fs,
                               OSM,
                               true);
