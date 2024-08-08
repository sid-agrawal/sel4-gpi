/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sel4/sel4.h>
#include <sel4test/test.h>
#include <sel4test/macros.h>
#include <vka/capops.h>
#include <sel4utils/thread.h>
#include "../test.h"
#include "../helpers.h"
#include <stdio.h>
#include <sel4bench/arch/sel4bench.h>
#include <utils/uthash.h>
#include <fcntl.h>
#include <sel4rpc/client.h>
#include <rpc.pb.h>

#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/bench_utils.h>
#include <sel4gpi/ads_client_context.h>
#include <sel4gpi/mo_client_context.h>
#include <sel4gpi/pd_client_context.h>
#include <sel4gpi/cpu_client_context.h>
#include <sel4gpi/pd_creation.h>

#include <fs_client.h>
#include <ramdisk_client.h>
#include "test_shared.h"

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

void benchmark_init(env_t env)
{
    sel4bench_init();

    for (int i = 0; i < 5; i++)
    {
        benchmark_ipc_rt(env);
    }
}

int benchmark_ipc_rt(env_t env)
{
    int error = 0;

    RpcMessage rpcMsg = {
        .which_msg = RpcMessage_bench_tag};

    error = sel4rpc_call(&env->rpc_client, &rpcMsg, 0, 0, 0);
    return error;
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

    benchmark_print_result(call_end - call_start);

    return 0;
}

static int benchmark_pd_create_sel4utils(env_t env, cspacepath_t *cspace_path, vka_object_t *cspace_obj)
{
    ccnt_t pd_create_start;
    ccnt_t pd_create_end;
    int error;
    TEST_LOG("\nPD CREATE");

    ccnt_t step_start, step_end;

    // For sel4utils, PD creation is just creating a cspace

    SEL4BENCH_READ_CCNT(pd_create_start);
    cspacepath_t dest;
    seL4_Word cspace_root_data;

#if 1
    error = vka_alloc_cnode_object(&env->vka, cspace_size_bits, cspace_obj);
    test_error_eq(error, 0);

// This was used to split the vka_alloc_cnode_object operation for timing individual steps
#else
    vka_object_t untyped;
    SEL4BENCH_READ_CCNT(step_start);
    error = vka_alloc_untyped(&env->vka, cspace_size_bits + seL4_SlotBits, &untyped);
    SEL4BENCH_READ_CCNT(step_end);
    printf("step 1: %lu, bits %lu\n", step_end - step_start, cspace_size_bits + seL4_SlotBits);
    test_error_eq(error, 0);

    cspacepath_t cnode_dest;
    SEL4BENCH_READ_CCNT(step_start);
    error = vka_cspace_alloc_path(&env->vka, &cnode_dest);
    SEL4BENCH_READ_CCNT(step_end);
    printf("step 2: %lu\n", step_end - step_start);
    test_error_eq(error, 0);

    SEL4BENCH_READ_CCNT(step_start);
    error = vka_untyped_retype(&untyped, seL4_CapTableObject, cspace_size_bits, 1, &cnode_dest);
    SEL4BENCH_READ_CCNT(step_end);
    printf("step 3: %lu, bits %lu, type %d\n", step_end - step_start, cspace_size_bits, seL4_CapTableObject);
    cspace_obj->type = seL4_CapTableObject;
    cspace_obj->cptr = cnode_dest.capPtr;
    cspace_obj->size_bits = cspace_size_bits;
    test_error_eq(error, 0);
#endif

    vka_cspace_make_path(&env->vka, cspace_obj->cptr, cspace_path);
    cspace_root_data = api_make_guard_skip_word(seL4_WordBits - cspace_size_bits);
    dest.capPtr = 1;
    dest.root = cspace_obj->cptr;
    dest.capDepth = cspace_size_bits;
    error = vka_cnode_mint(&dest, cspace_path, seL4_AllRights, cspace_root_data);
    test_error_eq(error, 0);

    SEL4BENCH_READ_CCNT(pd_create_end);

    benchmark_print_result(pd_create_end - pd_create_start);

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
    SEL4BENCH_READ_CCNT(pd_create_end);

    test_error_eq(error, 0);

    benchmark_print_result(pd_create_end - pd_create_start);

    return 0;
}

static int benchmark_pd_delete_sel4utils(env_t env, cspacepath_t *cspace_path, vka_object_t *cspace_obj)
{
    ccnt_t pd_delete_start;
    ccnt_t pd_delete_end;
    int error;
    TEST_LOG("\nPD DELETE");

    // For sel4utils, PD deletion is just deleting a cspace

    SEL4BENCH_READ_CCNT(pd_delete_start);
    error = vka_cnode_revoke(cspace_path);
    vka_free_object(&env->vka, cspace_obj);
    SEL4BENCH_READ_CCNT(pd_delete_end);

    test_error_eq(error, 0);

    benchmark_print_result(pd_delete_end - pd_delete_start);

    return 0;
}

static int benchmark_pd_delete_osm(pd_client_context_t *pd)
{
    ccnt_t pd_delete_start;
    ccnt_t pd_delete_end;
    int error;
    TEST_LOG("\nPD DELETE");

    // For osm, PD deletion is also revoking the PD cap

    SEL4BENCH_READ_CCNT(pd_delete_start);
    error = pd_client_terminate(pd);
    SEL4BENCH_READ_CCNT(pd_delete_end);

    test_error_eq(error, 0);

    benchmark_print_result(pd_delete_end - pd_delete_start);

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
    benchmark_print_result(pd_create_end_time - pd_create_start_time);
    return error;

#if CONFIG_MAX_NUM_NODES > 1
    seL4_TCB_GetAffinity_t affinity = seL4_TCB_GetAffinity(sel4utils_proc->thread.tcb.cptr);
    TEST_LOG("\naffinity: %lu", affinity.affinity);
#endif
    // seL4_DebugNameThread(proc.thread.tcb.cptr, "bench");
    // seL4_DebugDumpScheduler();
    return 0;
}

// This tests specifically a process PD, to compare with sel4test version
static int benchmark_pd_spawn_osm(pd_client_context_t *pd, seL4_CPtr *ep)
{
    ccnt_t pd_create_start_time;
    TEST_LOG("\nPD SPAWN");
    SEL4BENCH_READ_CCNT(pd_create_start_time);

    // Don't include EP creation in timing for consistency with spawn_pd_sel4utils
    ep_client_context_t hello_ep;
    int error = ep_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_EP), &hello_ep);
    test_error_eq(error, 0);
    *ep = hello_ep.raw_endpoint;

    SEL4BENCH_READ_CCNT(pd_create_start_time);

    // Configure the PD
    sel4gpi_runnable_t runnable = {0};
    pd_config_t *cfg = sel4gpi_configure_process("hello_benchmark", DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES, &runnable);
    test_assert(cfg != NULL);
    *pd = runnable.pd;

    // Send the parent endpoint to the PD
    // (XXX) Arya: Should this be subtracted from timing?
    seL4_CPtr slot;
    error = pd_client_send_cap(pd, hello_ep.raw_endpoint, &slot);
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
    benchmark_print_result(pd_create_end_time - pd_create_start_time);
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

    benchmark_print_result(send_cap_end - send_cap_start);

    return error;
}

static int benchmark_send_cap_osm(pd_client_context_t *pd)
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
    error = pd_client_send_cap(pd, mo.ep, &slot);
    SEL4BENCH_READ_CCNT(send_cap_end);

    test_error_eq(error, 0);

    benchmark_print_result(send_cap_end - send_cap_start);
    return error;
}

static int benchmark_frame_allocate_sel4utils(env_t env, vka_object_t *frame)
{
    int error;
    ccnt_t frame_alloc_start;
    ccnt_t frame_alloc_end;

    TEST_LOG("\nFRAME ALLOC");

    SEL4BENCH_READ_CCNT(frame_alloc_start);
    error = vka_alloc_frame(&env->vka, seL4_PageBits, frame);
    SEL4BENCH_READ_CCNT(frame_alloc_end);

    test_error_eq(error, 0);

    benchmark_print_result(frame_alloc_end - frame_alloc_start);

    return error;
}

static int benchmark_frame_allocate_osm(mo_client_context_t *mo)
{
    int error;
    ccnt_t frame_alloc_start;
    ccnt_t frame_alloc_end;

    TEST_LOG("\nFRAME ALLOC");

    SEL4BENCH_READ_CCNT(frame_alloc_start);
    error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO),
                                        1,
                                        MO_PAGE_BITS,
                                        mo);
    SEL4BENCH_READ_CCNT(frame_alloc_end);

    test_error_eq(error, 0);

    benchmark_print_result(frame_alloc_end - frame_alloc_start);

    return error;
}

static int benchmark_frame_free_sel4utils(env_t env, vka_object_t *frame)
{
    int error;
    ccnt_t frame_alloc_start;
    ccnt_t frame_alloc_end;

    TEST_LOG("\nFRAME FREE");

    SEL4BENCH_READ_CCNT(frame_alloc_start);
    vka_free_object(&env->vka, frame);
    SEL4BENCH_READ_CCNT(frame_alloc_end);

    benchmark_print_result(frame_alloc_end - frame_alloc_start);

    return error;
}

static int benchmark_frame_free_osm(mo_client_context_t *mo)
{
    int error;
    ccnt_t frame_alloc_start;
    ccnt_t frame_alloc_end;

    TEST_LOG("\nFRAME FREE");

    SEL4BENCH_READ_CCNT(frame_alloc_start);
    error = mo_component_client_disconnect(mo);
    SEL4BENCH_READ_CCNT(frame_alloc_end);

    benchmark_print_result(frame_alloc_end - frame_alloc_start);

    test_error_eq(error, 0);

    return error;
}

static int benchmark_ads_create_sel4utils(env_t env, vspace_t *vspace, vka_object_t *vspace_root,
                                          sel4utils_alloc_data_t **vspace_alloc_data)
{
    int error;
    ccnt_t ads_create_start;
    ccnt_t ads_create_end;

    TEST_LOG("\nADS CREATE");

    SEL4BENCH_READ_CCNT(ads_create_start);

    *vspace_alloc_data = calloc(1, sizeof(sel4utils_alloc_data_t));

    error = vka_alloc_vspace_root(&env->vka, vspace_root);
    test_error_eq(error, 0);

    error = assign_asid_pool(env->asid_pool, vspace_root->cptr);
    test_error_eq(error, 0);
    // why is there no 'unassign asid pool'?

    error = sel4utils_get_empty_vspace(&env->vspace, vspace, *vspace_alloc_data, &env->vka, vspace_root->cptr,
                                       NULL, NULL);
    test_error_eq(error, 0);
    SEL4BENCH_READ_CCNT(ads_create_end);

    benchmark_print_result(ads_create_end - ads_create_start);

    return error;
}

static int benchmark_ads_create_osm(ads_client_context_t *ads)
{
    int error;
    ccnt_t ads_create_start;
    ccnt_t ads_create_end;

    TEST_LOG("\nADS CREATE");

    seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);

    SEL4BENCH_READ_CCNT(ads_create_start);
    error = ads_component_client_connect(ads_rde, ads);
    SEL4BENCH_READ_CCNT(ads_create_end);

    benchmark_print_result(ads_create_end - ads_create_start);

    return error;
}

static int benchmark_ads_attach_sel4utils(env_t env, vspace_t *vspace, vka_object_t *frame, void **frame_addr)
{
    int error;
    ccnt_t ads_attach_start;
    ccnt_t ads_attach_end;
    void *mapped_vaddr;
    TEST_LOG("\nADS ATTACH");

    // Attach it to the ADS
    SEL4BENCH_READ_CCNT(ads_attach_start);

    // (XXX) Arya: Why was this call here?
    // seL4_CPtr cap_in_proc = sel4utils_copy_cap_to_process(&sel4utils_proc, &env->vka, bench_frame.cptr);

    reservation_t res = sel4utils_reserve_range_aligned(vspace, PAGE_SIZE_4K,
                                                        seL4_PageBits, seL4_AllRights, 1, &mapped_vaddr);
    test_assert(mapped_vaddr != NULL);

    error = sel4utils_map_pages_at_vaddr(vspace, &frame->cptr, NULL, mapped_vaddr, 1, seL4_PageBits, res);
    test_assert(mapped_vaddr != NULL);

    test_error_eq(error, 0);
    SEL4BENCH_READ_CCNT(ads_attach_end);

    benchmark_print_result(ads_attach_end - ads_attach_start);
    *frame_addr = mapped_vaddr;

    return error;
}

static int benchmark_ads_attach_osm(ads_client_context_t *ads, mo_client_context_t *mo, void **mo_vaddr)
{
    int error;
    ccnt_t ads_attach_start;
    ccnt_t ads_attach_end;
    ads_client_context_t vmr_rde = {.ep = sel4gpi_get_rde_by_space_id(ads->id, GPICAP_TYPE_VMR)};

    TEST_LOG("\nADS ATTACH");

    // Attach the MO to the ADS
    SEL4BENCH_READ_CCNT(ads_attach_start);
    error = ads_client_attach(&vmr_rde, NULL, mo, SEL4UTILS_RES_TYPE_GENERIC, mo_vaddr);
    SEL4BENCH_READ_CCNT(ads_attach_end);

    test_error_eq(error, 0);

    benchmark_print_result(ads_attach_end - ads_attach_start);

    return error;
}

static int benchmark_ads_remove_sel4utils(env_t env, vspace_t *vspace, void **frame_vaddr)
{
    int error;
    ccnt_t ads_remove_start;
    ccnt_t ads_remove_end;
    void *mapped_vaddr;
    TEST_LOG("\nADS REMOVE");

    // Remove the frame from the vspace
    SEL4BENCH_READ_CCNT(ads_remove_start);
    // Use VSPACE_PRESERVE so that we don't free the frame/slot while unmapping
    sel4utils_unmap_pages(vspace, frame_vaddr, 1, seL4_PageBits, VSPACE_PRESERVE);
    sel4utils_free_reservation_by_vaddr(vspace, frame_vaddr);
    SEL4BENCH_READ_CCNT(ads_remove_end);

    benchmark_print_result(ads_remove_end - ads_remove_start);

    return error;
}

static int benchmark_ads_remove_osm(ads_client_context_t *ads, void *mo_vaddr)
{
    int error;
    ccnt_t ads_remove_start;
    ccnt_t ads_remove_end;
    TEST_LOG("\nADS REMOVE");

    ads_client_context_t vmr_rde = {.ep = sel4gpi_get_rde_by_space_id(ads->id, GPICAP_TYPE_VMR)};

    // Remove the MO from the ADS
    SEL4BENCH_READ_CCNT(ads_remove_start);
    error = ads_client_rm(&vmr_rde, mo_vaddr);
    SEL4BENCH_READ_CCNT(ads_remove_end);

    test_error_eq(error, 0);

    benchmark_print_result(ads_remove_end - ads_remove_start);

    return error;
}

static int benchmark_ads_delete_sel4utils(env_t env, vspace_t *vspace, vka_object_t *vspace_root,
                                          sel4utils_alloc_data_t *vspace_alloc_data)
{
    ccnt_t ads_delete_start;
    ccnt_t ads_delete_end;
    int error;
    TEST_LOG("\nADS_DELETE");

    SEL4BENCH_READ_CCNT(ads_delete_start);
    vspace_tear_down(vspace, VSPACE_FREE);
    vka_free_object(&env->vka, vspace_root);
    free(vspace_alloc_data);
    SEL4BENCH_READ_CCNT(ads_delete_end);

    benchmark_print_result(ads_delete_end - ads_delete_start);

    return 0;
}

static int benchmark_ads_delete_osm(ads_client_context_t *ads)
{
    ccnt_t ads_delete_start;
    ccnt_t ads_delete_end;
    int error;
    TEST_LOG("\nADS_DELETE");

    SEL4BENCH_READ_CCNT(ads_delete_start);
    error = ads_client_disconnect(ads);
    SEL4BENCH_READ_CCNT(ads_delete_end);

    test_error_eq(error, 0);

    benchmark_print_result(ads_delete_end - ads_delete_start);

    return 0;
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

    benchmark_print_result(cpu_create_end - cpu_create_start);

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

    benchmark_print_result(cpu_create_end - cpu_create_start);

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
                               0, vspace_root, 0, (seL4_Word)ipc_buf_vaddr, ipc_buf_frame);
    SEL4BENCH_READ_CCNT(cpu_bind_end);
    test_error_eq(error, 0);

    benchmark_print_result(cpu_bind_end - cpu_bind_start);

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

    benchmark_print_result(cpu_bind_end - cpu_bind_start);

    return error;
}

static int benchmark_cpu_delete_sel4utils(env_t env, vka_object_t *tcb)
{
    ccnt_t cpu_delete_start;
    ccnt_t cpu_delete_end;
    int error;
    TEST_LOG("\nCPU_DELETE");

    SEL4BENCH_READ_CCNT(cpu_delete_start);
    vka_free_object(&env->vka, tcb);
    SEL4BENCH_READ_CCNT(cpu_delete_end);

    test_error_eq(error, 0);

    benchmark_print_result(cpu_delete_end - cpu_delete_start);

    return 0;
}

static int benchmark_cpu_delete_osm(cpu_client_context_t *cpu)
{
    ccnt_t cpu_delete_start;
    ccnt_t cpu_delete_end;
    int error;
    TEST_LOG("\nCPU_DELETE");

    SEL4BENCH_READ_CCNT(cpu_delete_start);
    error = cpu_component_client_disconnect(cpu);
    SEL4BENCH_READ_CCNT(cpu_delete_end);

    test_error_eq(error, 0);

    benchmark_print_result(cpu_delete_end - cpu_delete_start);

    return 0;
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
    gpi_obj_id_t ramdisk_id;
    seL4_CPtr ramdisk_pd_cap;
    error = start_ramdisk_pd(&ramdisk_pd_cap, &ramdisk_id);
    test_assert(error == 0);

    /* Start fs server process */
    gpi_obj_id_t fs_id;
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

    benchmark_print_result(fs_open_end - fs_open_start);

    /* Remove RDEs from test process so that it won't be cleaned up by recursive cleanup */
    error = pd_client_remove_rde(&pd_conn, sel4gpi_get_resource_type_code(BLOCK_RESOURCE_TYPE_NAME), BADGE_SPACE_ID_NULL);
    test_assert(error == 0);

    error = pd_client_remove_rde(&pd_conn, sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME), BADGE_SPACE_ID_NULL);
    test_assert(error == 0);

    // And remove the file for the same reason
    error = close(f);
    test_assert(error == 0);

    // Terminate PDs
    pd_client_context_t fs_context = {.ep = fs_pd_cap};
    test_error_eq(maybe_terminate_pd(&fs_context), 0);

    pd_client_context_t ramdisk_context = {.ep = ramdisk_pd_cap};
    test_error_eq(maybe_terminate_pd(&ramdisk_context), 0);

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

        printf("%lu\n", end - start);
    }

    return 0;
}
#endif

/**
 * Benchmark:
 * + cspace create
 * + vspace create
 * + attach a frame to the vspace
 * + cpu create
 * + bind vspace/cspace to cpu
 */
int benchmark_basic_sel4utils(env_t env)
{
    int error = 0;

    benchmark_init(env);

    // PD create
    cspacepath_t cspace;
    vka_object_t cspace_obj;
    error = benchmark_pd_create_sel4utils(env, &cspace, &cspace_obj);
    test_error_eq(error, 0);

    // Frame create
    vka_object_t ipc_frame;
    error = benchmark_frame_allocate_sel4utils(env, &ipc_frame);
    test_error_eq(error, 0);

    // ADS create / attach
    vspace_t vspace;
    vka_object_t vspace_root;
    sel4utils_alloc_data_t *vspace_alloc_data;
    error = benchmark_ads_create_sel4utils(env, &vspace, &vspace_root, &vspace_alloc_data);
    test_error_eq(error, 0);

    void *ipc_frame_addr;
    error = benchmark_ads_attach_sel4utils(env, &vspace, &ipc_frame, &ipc_frame_addr);
    test_error_eq(error, 0);

    // CPU create / bind
    vka_object_t tcb;
    error = benchmark_cpu_create_sel4utils(env, &tcb);
    test_error_eq(error, 0);

    error = benchmark_cpu_bind_sel4utils(env, &tcb, cspace.capPtr, vspace.get_root(&vspace),
                                         ipc_frame.cptr, ipc_frame_addr);
    test_error_eq(error, 0);

    // ADS remove
    error = benchmark_ads_remove_sel4utils(env, &vspace, ipc_frame_addr);
    test_error_eq(error, 0);

    // Frame delete
    error = benchmark_frame_free_sel4utils(env, &ipc_frame);
    test_error_eq(error, 0);

    // PD delete
    error = benchmark_pd_delete_sel4utils(env, &cspace, &cspace_obj);
    test_error_eq(error, 0);

    // CPU delete
    error = benchmark_cpu_delete_sel4utils(env, &tcb);
    test_error_eq(error, 0);

    // ADS delete
    error = benchmark_ads_delete_sel4utils(env, &vspace, &vspace_root, vspace_alloc_data);
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
int benchmark_basic_osm(env_t env)
{
    int error = 0;

    benchmark_init(env);

    // PD create
    pd_client_context_t pd;
    error = benchmark_pd_create_osm(&pd);
    test_error_eq(error, 0);

    // Frame create
    mo_client_context_t ipc_frame_mo;
    error = benchmark_frame_allocate_osm(&ipc_frame_mo);
    test_error_eq(error, 0);

    // ADS bench
    ads_client_context_t ads;
    error = benchmark_ads_create_osm(&ads);
    test_error_eq(error, 0);

    void *ipc_frame_addr;
    error = benchmark_ads_attach_osm(&ads, &ipc_frame_mo, &ipc_frame_addr);
    test_error_eq(error, 0);

    // CPU bench
    cpu_client_context_t cpu;
    error = benchmark_cpu_create_osm(&cpu);
    test_error_eq(error, 0);

    error = benchmark_cpu_bind_osm(&cpu, &ads, &pd, &ipc_frame_mo, ipc_frame_addr);
    test_error_eq(error, 0);

    // ADS remove
    error = benchmark_ads_remove_osm(&ads, ipc_frame_addr);
    test_error_eq(error, 0);

    // Frame delete
    error = benchmark_frame_free_osm(&ipc_frame_mo);
    test_error_eq(error, 0);

    // PD destroy (doesn't cleanup ADS/CPU, PD has no resources to cleanup)
    error = benchmark_pd_delete_osm(&pd);
    test_error_eq(error, 0);

    // CPU destroy
    error = benchmark_cpu_delete_osm(&cpu);
    test_error_eq(error, 0);

    // ADS destroy
    error = benchmark_ads_delete_osm(&ads);
    test_error_eq(error, 0);

    sel4bench_destroy();
    return sel4test_get_result();
}

/**
 * Benchmark process spawn
 * + sending cap to the spawned process
 * + sending IPC to the spaned process
 */
int benchmark_process_spawn_sel4utils(env_t env)
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
int benchmark_process_spawn_osm(env_t env)
{
    int error = 0;

    benchmark_init(env);

    // Run benchmarks
    pd_client_context_t pd;
    seL4_CPtr ep;
    error = benchmark_pd_spawn_osm(&pd, &ep);
    test_error_eq(error, 0);

    error = benchmark_send_cap_osm(&pd);
    test_error_eq(error, 0);

    error = benchmark_ipc_pd(ep);
    test_error_eq(error, 0);

    // Cleanup
    test_error_eq(maybe_terminate_pd(&pd), 0);

    sel4bench_destroy();
    return sel4test_get_result();
}

/**
 * Benchmark the time to cleanup a process
 * This depends on the cleanup policy, set by the CMake options GPI_CLEANUP_PD_DEPTH & GPI_CLEANUP_RS_DEPTH
 *
 * This is a test scenario with toy servers (Block, File, and DB servers)
 * The servers have no real functionality, so this tests the root task's cleanup overhead only.
 */
static int internal_benchmark_cleanup_toy_servers(env_t env, hello_cleanup_mode_t server_to_crash)
{
    int error = 0;

    benchmark_init(env);

    int n_requests = 10;

    /* Create EP to listen for test results */
    ep_client_context_t self_ep;
    error = sel4gpi_alloc_endpoint(&self_ep);
    test_assert(error == 0);

    /* Start the PDs */
    pd_client_context_t hello_server_toy_block_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_BLOCK_SERVER_MODE, n_requests,
                                      &self_ep, &hello_server_toy_block_pd);
    test_assert(error == 0);

    pd_client_context_t hello_server_toy_file_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_FILE_SERVER_MODE, n_requests,
                                      &self_ep, &hello_server_toy_file_pd);
    test_assert(error == 0);

    pd_client_context_t hello_server_toy_db_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_DB_SERVER_MODE, n_requests,
                                      &self_ep, &hello_server_toy_db_pd);
    test_assert(error == 0);

    pd_client_context_t hello_client_toy_block_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_BLOCK_CLIENT_MODE, n_requests,
                                      &self_ep, &hello_client_toy_block_pd);
    test_assert(error == 0);

    pd_client_context_t hello_client_toy_file_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_FILE_CLIENT_MODE, n_requests,
                                      &self_ep, &hello_client_toy_file_pd);
    test_assert(error == 0);

    pd_client_context_t hello_client_toy_db_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_DB_CLIENT_MODE, n_requests,
                                      &self_ep, &hello_client_toy_db_pd);
    test_assert(error == 0);

    pd_client_context_t hello_dummy_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_NOTHING_MODE, n_requests,
                                      &self_ep, &hello_dummy_pd);
    test_assert(error == 0);

    /* Remove RDEs from test process so that it won't be cleaned up by recursive cleanup */
    gpi_cap_t toy_block_type = sel4gpi_get_resource_type_code(TOY_BLOCK_SERVER_RESOURCE_TYPE);
    gpi_cap_t toy_file_type = sel4gpi_get_resource_type_code(TOY_FILE_SERVER_RESOURCE_TYPE);
    gpi_cap_t toy_db_type = sel4gpi_get_resource_type_code(TOY_DB_SERVER_RESOURCE_TYPE);
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();

    error = pd_client_remove_rde(&pd_conn, toy_block_type, BADGE_SPACE_ID_NULL);
    test_assert(error == 0);

    error = pd_client_remove_rde(&pd_conn, toy_file_type, BADGE_SPACE_ID_NULL);
    test_assert(error == 0);

    error = pd_client_remove_rde(&pd_conn, toy_db_type, BADGE_SPACE_ID_NULL);
    test_assert(error == 0);

    /* Wait for clients to finish making requests */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    for (int i = 0; i < 4; i++)
    {
        tag = seL4_Recv(self_ep.raw_endpoint, NULL);
        error = seL4_MessageInfo_get_label(tag);
        test_assert(error == 0);
    }

    /* Crash a PD */
    ccnt_t start, end;

    if (server_to_crash == HELLO_CLEANUP_TOY_BLOCK_SERVER_MODE)
    {
        printf("Crashing toy_block_server PD\n");
        SEL4BENCH_READ_CCNT(start);
        error = pd_client_terminate(&hello_server_toy_block_pd);
        SEL4BENCH_READ_CCNT(end);
        test_assert(error == 0);
    }
    else if (server_to_crash == HELLO_CLEANUP_TOY_FILE_SERVER_MODE)
    {
        printf("Crashing toy_block_server PD\n");
        SEL4BENCH_READ_CCNT(start);
        error = pd_client_terminate(&hello_server_toy_file_pd);
        SEL4BENCH_READ_CCNT(end);
        test_assert(error == 0);
    }
    else if (server_to_crash == HELLO_CLEANUP_TOY_DB_SERVER_MODE)
    {
        printf("Crashing toy_block_server PD\n");
        SEL4BENCH_READ_CCNT(start);
        error = pd_client_terminate(&hello_server_toy_db_pd);
        SEL4BENCH_READ_CCNT(end);
        test_assert(error == 0);
    }
    else
    {
        // Invalid mode of server to crash
        test_assert(0);
    }
    benchmark_print_result(end - start);

    /* Cleanup other PDs */
    test_error_eq(maybe_terminate_pd(&hello_server_toy_block_pd), 0);
    test_error_eq(maybe_terminate_pd(&hello_server_toy_file_pd), 0);
    test_error_eq(maybe_terminate_pd(&hello_server_toy_db_pd), 0);
    test_error_eq(maybe_terminate_pd(&hello_client_toy_block_pd), 0);
    test_error_eq(maybe_terminate_pd(&hello_client_toy_file_pd), 0);
    test_error_eq(maybe_terminate_pd(&hello_client_toy_db_pd), 0);
    test_error_eq(maybe_terminate_pd(&hello_dummy_pd), 0);

    sel4bench_destroy();
    return sel4test_get_result();
}

/**
 * Test cleanup policy of toy servers, where we crash the first level toy server (the block server)
 */
int benchmark_cleanup_toy_servers_1(env_t env)
{
    internal_benchmark_cleanup_toy_servers(env, HELLO_CLEANUP_TOY_BLOCK_SERVER_MODE);
}

/**
 * Test cleanup policy of toy servers, where we crash the second level toy server (the file server)
 */
int benchmark_cleanup_toy_servers_2(env_t env)
{
    internal_benchmark_cleanup_toy_servers(env, HELLO_CLEANUP_TOY_FILE_SERVER_MODE);
}

/**
 * Test cleanup policy of toy servers, where we crash the third level toy server (the db server)
 */
int benchmark_cleanup_toy_servers_3(env_t env)
{
    internal_benchmark_cleanup_toy_servers(env, HELLO_CLEANUP_TOY_DB_SERVER_MODE);
}

typedef enum _cleanup_scenario_server
{
    CLEANUP_RAMDISK,
    CLEANUP_FS
} cleanup_scenario_server_t;

/**
 * Benchmark the time to cleanup a process
 * This depends on the cleanup policy, set by the CMake options GPI_CLEANUP_PD_DEPTH & GPI_CLEANUP_RS_DEPTH
 *
 * This is a scenario with real servers (Ramdisk, File Server, and KVstore)
 * This tests both the root task's cleanup work, and the cleanup time for actual servers
 * Eg. the servers may clean up destroyed resource spaces once the root task notifies them
 */
static int internal_benchmark_cleanup(env_t env, cleanup_scenario_server_t server_to_crash)
{
    int error = 0;

    benchmark_init(env);

    int n_requests = 10;

    /* Create EP to listen for test results */
    ep_client_context_t self_ep;
    error = sel4gpi_alloc_endpoint(&self_ep);
    test_assert(error == 0);

    /* Start the PDs */
    pd_client_context_t ramdisk_pd, fs_pd, kvstore_server_pd, kvstore_client_pd;

    assert(!"Not implemented");

    /* Remove RDEs from test process so that it won't be cleaned up by recursive cleanup */
    gpi_cap_t block_type = sel4gpi_get_resource_type_code(BLOCK_RESOURCE_TYPE_NAME);
    gpi_cap_t file_type = sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME);
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();

    error = pd_client_remove_rde(&pd_conn, block_type, BADGE_SPACE_ID_NULL);
    test_assert(error == 0);

    error = pd_client_remove_rde(&pd_conn, file_type, BADGE_SPACE_ID_NULL);
    test_assert(error == 0);

    /* Wait for clients to finish making requests */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    for (int i = 0; i < 4; i++)
    {
        tag = seL4_Recv(self_ep.raw_endpoint, NULL);
        error = seL4_MessageInfo_get_label(tag);
        test_assert(error == 0);
    }

    /* Crash a PD */
    ccnt_t start, end;

    if (server_to_crash == CLEANUP_RAMDISK)
    {
        printf("Crashing toy_block_server PD\n");
        SEL4BENCH_READ_CCNT(start);
        error = pd_client_terminate(&ramdisk_pd);
        SEL4BENCH_READ_CCNT(end);
        test_assert(error == 0);
    }
    else if (server_to_crash == CLEANUP_FS)
    {
        printf("Crashing toy_block_server PD\n");
        SEL4BENCH_READ_CCNT(start);
        error = pd_client_terminate(&fs_pd);
        SEL4BENCH_READ_CCNT(end);
        test_assert(error == 0);
    }
    else
    {
        // Invalid mode of server to crash
        test_assert(0);
    }
    benchmark_print_result(end - start);

    /* Cleanup other PDs */
    test_error_eq(maybe_terminate_pd(&ramdisk_pd), 0);
    test_error_eq(maybe_terminate_pd(&fs_pd), 0);
    test_error_eq(maybe_terminate_pd(&kvstore_client_pd), 0);
    test_error_eq(maybe_terminate_pd(&kvstore_server_pd), 0);

    sel4bench_destroy();
    return sel4test_get_result();
}

int benchmark_cleanup_ramdisk(env_t env)
{
    return internal_benchmark_cleanup(env, CLEANUP_RAMDISK);
}

int benchmark_cleanup_fs(env_t env)
{
    return internal_benchmark_cleanup(env, CLEANUP_FS);
}

#ifdef GPI_BENCHMARK_MULTIPLE

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM001,
                               "sel4utils basic bench",
                               benchmark_basic_sel4utils,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM002,
                               "OSM basic bench",
                               benchmark_basic_osm,
                               OSM,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM003,
                               "sel4utils bench process spawn / send cap",
                               benchmark_process_spawn_sel4utils,
                               BASIC,
                               true);

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM004,
                               "osm bench process spawn / send cap",
                               benchmark_process_spawn_osm,
                               OSM,
                               true);

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM005,
                               "osm FILE create",
                               benchmark_fs,
                               OSM,
                               true);

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM006,
                               "osm toy server cleanup, crash block server",
                               benchmark_cleanup_toy_servers_1,
                               OSM,
                               true);

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM007,
                               "osm toy server cleanup, crash file server",
                               benchmark_cleanup_toy_servers_2,
                               OSM,
                               true);

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM008,
                               "osm toy server cleanup, crash db server",
                               benchmark_cleanup_toy_servers_3,
                               OSM,
                               true);

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM09,
                               "osm crash ramdisk server",
                               benchmark_cleanup_ramdisk,
                               OSM,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM010,
                               "osm crash fs server",
                               benchmark_cleanup_fs,
                               OSM,
                               true)

#else

DEFINE_TEST(GPIBM001,
            "sel4utils basic bench",
            benchmark_basic_sel4utils,
            true)

DEFINE_TEST_OSM(GPIBM002,
                "OSM basic bench",
                benchmark_basic_osm,
                true)

DEFINE_TEST(GPIBM003,
            "sel4utils bench process spawn / send cap",
            benchmark_process_spawn_sel4utils,
            true);

DEFINE_TEST_OSM(GPIBM004,
                "osm bench process spawn / send cap",
                benchmark_process_spawn_osm,
                true);

DEFINE_TEST_OSM(GPIBM005,
                "osm FILE create",
                benchmark_fs,
                true);

DEFINE_TEST_OSM(GPIBM006,
                "osm toy server cleanup, crash block server",
                benchmark_cleanup_toy_servers_1,
                true);

DEFINE_TEST_OSM(GPIBM007,
                "osm toy server cleanup, crash file server",
                benchmark_cleanup_toy_servers_2,
                true);

DEFINE_TEST_OSM(GPIBM008,
                "osm toy server cleanup, crash db server",
                benchmark_cleanup_toy_servers_3,
                true);

DEFINE_TEST(GPIBM09,
            "osm crash ramdisk server",
            benchmark_cleanup_ramdisk,
            false)

DEFINE_TEST(GPIBM010,
            "osm crash fs server",
            benchmark_cleanup_fs,
            false)

#endif