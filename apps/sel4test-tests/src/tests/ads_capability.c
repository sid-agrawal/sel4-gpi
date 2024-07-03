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

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/cpu_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/mo_component.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/pd_utils.h>

#include <sel4bench/arch/sel4bench.h>

int test_ads_attach(env_t env)
{
    // Initialize the ADS
    ads_client_context_t ads_conn = sel4gpi_get_bound_vmr_rde();

    // allocate a new MO
    int n_pages = 5;
    mo_client_context_t mo_conn;
    int error = mo_component_client_connect(env->gpi_endpoint,
                                            n_pages,
                                            &mo_conn);
    test_error_eq(error, 0);

    // attacht the MO
    void *ret_vaddr;
    error = ads_client_attach(&ads_conn,
                              0, /*vaddr*/
                              &mo_conn,
                              &ret_vaddr);
    assert(error == 0);
    assert(ret_vaddr != NULL);

    // access the MO
    printf("Attached MO at vaddr: %p\n", ret_vaddr);

    for (int i = 0; i < n_pages; i++)
    {
        printf("MO[%u]: %u\n", i, ((char *)ret_vaddr)[i * PAGE_SIZE_4K]);
    }
    printf("Finished reading the new MO: %p\n", ret_vaddr);

    return sel4test_get_result();
}
DEFINE_TEST(GPIADS010, "Ensure the ads attach works", test_ads_attach, true)

int test_ads_rm(env_t env)
{
    // Initialize the ADS
    ads_client_context_t ads_conn = sel4gpi_get_bound_vmr_rde();

    // allocate a new MO
    int n_pages = 5;
    mo_client_context_t mo_conn;
    int error = mo_component_client_connect(env->gpi_endpoint,
                                            n_pages,
                                            &mo_conn);
    test_error_eq(error, 0);

    // attach the MO
    void *ret_vaddr;
    error = ads_client_attach(&ads_conn,
                              0, /*vaddr*/
                              &mo_conn,
                              &ret_vaddr);

    assert(error == 0);
    assert(ret_vaddr != NULL);

    // remove the MO
    error = ads_client_rm(&ads_conn,
                          ret_vaddr);

    assert(error == 0);

    // check that the vaddr is inaccessible
    // uncomment the following line to check, it should cause a page fault
    // printf("There should be a page fault after this...");
    // printf("MO: %u\n", ((char *)ret_vaddr)[0]);

    return sel4test_get_result();
}
DEFINE_TEST(GPIADS011, "Ensure the ads remove works", test_ads_rm, true)

// (XXX) Arya: Pending update to new APIs
#if 0
int test_ads_dump_rr(env_t env)
{
    int error;
    cspacepath_t path;
    vka_cspace_make_path(&env->vka, env->self_ads_cptr, &path);
    ads_client_context_t conn = sel4gpi_get_bound_vmr_rde();

    // Using a known EP, get a new ads CAP.
    ads_client_context_t shallow_copy_conn;
    error = ads_client_shallow_copy(&conn, &env->vka, (void *)0x10001000, &shallow_copy_conn);
    test_error_eq(error, 0);

    cpu_client_context_t cpu_conn;
    error = cpu_component_client_connect(env->gpi_endpoint,
                                         &cpu_conn);
    test_error_eq(error, 0);

    // Config its ads and cspace
    error = cpu_client_config(&cpu_conn,
                              &shallow_copy_conn,
                              env->cspace_root,
                              env->endpoint);
    test_error_eq(error, 0);

    // Dump the ads rr
    size_t buf_num_pages = 15;
    char *ads_rr = malloc(buf_num_pages * 4096);
    assert(ads_rr != NULL);

    ads_client_dump_rr(&conn, ads_rr, buf_num_pages * 4096);
    printf("ADS RR: \n%s\n", ads_rr);

    return sel4test_get_result();
}
DEFINE_TEST(GPIADS006, "Dump the RR of the ads", test_ads_dump_rr, true)

int test_ads_shallow_copy(env_t env)
{
    int error;
    cspacepath_t path;
    vka_cspace_make_path(&env->vka, env->self_ads_cptr, &path);
    ads_client_context_t conn = sel4gpi_get_bound_vmr_rde();

    // arm_init_ccnt();
    sel4bench_init();

    // Using a known EP, get a new ads CAP.
    ads_client_context_t ads_conn_shallow_copy1;
    // uint64_t start = timestamp_sid();
    // error = ads_client_shallow_copy(&conn, &env->vka,  (void *) 0x10001000, &ads_conn_shallow_copy1);
    // test_error_eq(error, 0);

    // uint64_t end = timestamp_sid();
    /* calculate diff in ns */
    // uint64_t diff = (end - start);
    // printf("Time taken to shallow_copy: %llu cycles\n", diff);
    // printf("Time taken to shallow_copy: %llu million cycles\n", diff/(1000*1000));

    ads_client_context_t ads_conn_shallow_copy2;
    ccnt_t start, end;
    COMPILER_MEMORY_FENCE();
    // isb();

    printf("DOing something ....\n");
    printf("DOing something ....\n");
    printf("DOing something ....\n");
    printf("DOing something ....\n");

    // isb();
    SEL4BENCH_READ_CCNT(end);
    COMPILER_MEMORY_FENCE();
    printf("Time taken to shallow_copy: %lu cycles\n", end - start);
    // error = ads_client_shallow_copy(&ads_conn_shallow_copy1, &env->vka,  (void *) 0x10001000, &ads_conn_shallow_copy2);
    test_error_eq(error, 0);

    return sel4test_get_result();
}
// DEFINE_TEST(GPIADS001, "Ensure that as shallow_copy works", test_ads_shallow_copy, true)

vka_object_t ep_for_thread;

void test_func_die(seL4_Word arg0, seL4_Word arg1, seL4_Word arg2)
{
    ccnt_t ctx_start, ctx_end;
    ccnt_t creation_start, creation_end;
    sel4bench_init();
    SEL4BENCH_READ_CCNT(creation_end);
    printf("%s: END TIME: %lu\n", __func__, creation_end);

    // This is nasty hack to get the IPC buffers address for the thread.
    seL4_SetIPCBuffer((seL4_IPCBuffer *)arg0);

    assert(ep_for_thread.cptr != 0);
    uint64_t shared_var_stack;
    // printf("test_func_die: addr of var on this stack: %p\n", &shared_var_stack);

    // Send IPC back to main thread.
    /* set the data to send. We smains_epend it in the first message register */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, (seL4_Word)&shared_var_stack);
    SEL4BENCH_READ_CCNT(ctx_start);
    tag = seL4_Call(ep_for_thread.cptr, tag);
    SEL4BENCH_READ_CCNT(ctx_end);

    creation_start = seL4_GetMR(0);
    // printf("%s: START TIME: %lu\n", __func__, creation_start);
    printf("test_func_die: Creating Time: %lu cycles\n", (creation_end - creation_start) / 1000);
    // printf("test_func_die: Cross AS IPC Time: %lu cycles\n", ctx_end - ctx_start);
    //*other_stack = 0xdeadbeef;

    float data[1000];
    // printf("test_func_die: addr of var on other stack: %p\n", other_stack);
    printf("test_func_die: Cross AS IPC Time\n");
    int count = 1000;
    int i = 0;
    for (int i = 0; i < count; i++)
    {

        tag = seL4_MessageInfo_new(0, 0, 0, 1);
        SEL4BENCH_READ_CCNT(ctx_start);
        tag = seL4_Call(ep_for_thread.cptr, tag);
        SEL4BENCH_READ_CCNT(ctx_end);
        data[i] = (ctx_end - ctx_start) / 2;
    }

    float mean, sd;
    calculateSD(data, &mean, &sd, 1, 99);
    printf("MEAN: %f, SD: %f \n", mean / 1000, sd / 1000);

    while (1)
        ;
}

int test_ads_stack_isolated_stack_die(env_t env)
{
    printf("------------------- STARTING : %s -------------------\n", __func__);
    int error;
    cspacepath_t path;
    vka_cspace_make_path(&env->vka, env->self_ads_cptr, &path);
    ads_client_context_t conn = sel4gpi_get_bound_vmr_rde();

    sel4bench_init();
    // Make new PD i.e. CSspace
    ccnt_t start;
    SEL4BENCH_READ_CCNT(start);

    error = vka_alloc_endpoint(&env->vka, &ep_for_thread);
    assert(error == 0);

    // This shared var is on the stack an hence should not work.
    volatile uint64_t shared_var_stack = 1;

    // Using a known EP, get a new ads CAP.
    ads_client_context_t ads_conn_shallow_copy1;
    error = ads_client_shallow_copy(&conn, &env->vka, (void *)0x10001000, &ads_conn_shallow_copy1);
    test_error_eq(error, 0);

    // TODO: Attach a new stack, this is done inside clinet_config for now.
    // stack_cap
    // ads_client_attach()
    // Attach a new stack

    // Allocate new CPU
    cpu_client_context_t cpu_conn;
    error = cpu_component_client_connect(env->gpi_endpoint,
                                         &cpu_conn);
    test_error_eq(error, 0);

    // Config its ads and cspace
    error = cpu_client_config(&cpu_conn,
                              &ads_conn_shallow_copy1,
                              env->cspace_root,
                              env->endpoint);
    test_error_eq(error, 0);

    // Start it.
    // Add args.
    error = cpu_client_start(&cpu_conn, test_func_die);
    test_error_eq(error, 0);

    OSDB_PRINTF(ADS_DEBUG, "%d: main_thread: shared_var(%p) = %ld\n", __LINE__, &shared_var_stack, shared_var_stack);
    shared_var_stack = 4;

    //  uint64_t *other_thread_stack = (uintptr_t*)0x10022fb8;
    //  *other_thread_stack = 5;

    // Wait for test_func_die to reply.

    /* Wait for the thread to finish */

    seL4_MessageInfo_t tag;
    tag = seL4_MessageInfo_new(0, 0, 0, 0);

    tag = seL4_Recv(ep_for_thread.cptr, NULL);
    assert(seL4_MessageInfo_get_length(tag) == 1);
    uint64_t *other_thread_stack = (uintptr_t *)seL4_GetMR(0);
    OSDB_PRINTF(ADS_DEBUG, "root-task: \t Writing to Other thread's stack: %p\n", other_thread_stack);

    /* Comment the line below for the test to pass*/
    //*other_thread_stack = 5;

    /* modify the message */
    seL4_Word main_thread_stack = 5;
    seL4_SetMR(0, (seL4_Word)start);
    seL4_ReplyRecv(ep_for_thread.cptr, tag, NULL);
    printf("------------------- Phase 2 : %s -------------------\n", __func__);

    while (1)
    {
        //  printf("main responding to other thread\n");
        seL4_ReplyRecv(ep_for_thread.cptr, tag, NULL);
    }
    printf("------------------- ENDING : %s -------------------\n", __func__);

    // printf("%d: main_thread: shared_var(%p) = %d\n", __LINE__, &shared_var_stack, shared_var_stack);

    // Send a message to the thread.
    seL4_DebugDumpScheduler();

    return sel4test_get_result();
}
DEFINE_TEST(GPIADS002, "Ensure that thread stack works", test_ads_stack_isolated_stack_die, true)
#endif

// (XXX) Arya: This was commented out before, not sure what it is for
#if 0
#include <time.h>
typedef uint64_t timestamp_t;
typedef uint64_t word_t;

#define PMUSERENR_EL0 "PMUSERENR_EL0"
#define CCNT "PMCCNTR_EL0"
#define PMCR "PMCR_EL0"
#define PMCNTENSET "PMCNTENSET_EL0"
#define PMINTENSET "PMINTENSET_EL1"
#define PMOVSR "PMOVSCLR_EL0"
#define CCNT_INDEX 31

#define PMCR_ENABLE 0
#define PMCR_ECNT_RESET 1
#define PMCR_CCNT_RESET 2

#define MRS(reg, v) asm volatile("mrs %x0," reg : "=r"(v))
#define MSR(reg, v)                                \
    do                                             \
    {                                              \
        word_t _v = v;                             \
        asm volatile("msr " reg ",%x0" ::"r"(_v)); \
    } while (0)

#define SYSTEM_WRITE_WORD(reg, v) MSR(reg, v)
#define SYSTEM_READ_WORD(reg, v) MRS(reg, v)

void arm_init_ccnt(void)
{



    //SYSTEM_WRITE_WORD(PMUSERENR_EL0, 4);
    uint32_t val = (BIT(PMCR_ENABLE) | BIT(PMCR_CCNT_RESET) | BIT(PMCR_ECNT_RESET));
    SYSTEM_WRITE_WORD(PMCR, val);

#ifdef PMCNTENSET
    /* turn on the cycle counter */
    SYSTEM_WRITE_WORD(PMCNTENSET, BIT(CCNT_INDEX));
#endif

#ifdef CONFIG_ARM_ENABLE_PMU_OVERFLOW_INTERRUPT
    armv_enableOverflowIRQ();
#endif /* CONFIG_ARM_ENABLE_PMU_OVERFLOW_INTERRUPT */
}

static inline timestamp_t timestamp_sid(void)
{
    timestamp_t ccnt;
    SYSTEM_READ_WORD(CCNT, ccnt);
    return ccnt;
}
#endif

#ifdef WQEAREREADY
int test_ads_bind_cpu(env_t env)
{
    ads_client_context_t conn;

    // Using a known EP, get a new ads CAP.
    int error = ads_component_client_connect(env->gpi_endpoint, &conn);
    test_error_eq(error, 0);

    // Increment the ads cap.
    seL4_TCB tcb = 0; // Get a new CPU cap
    error = ads_client_bind_cpu(&conn, tcb);
    test_error_eq(error, 0);

    // Decrement the cap. TODO(siagraw)
    // Delete the ads cap. TODO(siagraw)
    return sel4test_get_result();
}
DEFINE_TEST(GPIADS003, "Ensure the ads bind to cpu works", test_ads_bind_cpu, true)

int test_ads_shallow_copy(env_t env)
{
    ads_client_context_t conn;

    // Using a known EP, get a new ads CAP.
    int error = ads_component_client_connect(env->gpi_endpoint, &conn);
    test_error_eq(error, 0);

    // Increment the ads cap.
    error = 0; // Call shallow_copy
    test_error_eq(error, 0);

    // Decrement the cap. TODO(siagraw)
    // Delete the ads cap. TODO(siagraw)
    return sel4test_get_result();
}
DEFINE_TEST(GPIADS004, "Ensure the ads shallow_copy works", test_ads_shallow_copy, true)

int test_ads_stack_isolated(env_t env)
{
    ads_client_context_t conn;
    
    // Using a known EP, get a new ads CAP.
    int error = ads_component_client_connect(env->gpi_endpoint, &conn);
    test_error_eq(error, 0);

    // shallow_copy the ads,
    // ads_client_shallow_copy(&conn, 0, 0, 0);
    // Attach a new stack
    // stack_cap
    // ads_client_shallow_copy(&conn, 0, 0, 0);
    // Attach a new stack
    // Allocate a new PD i.e. cspace.
    // Allocate a new TCB and attach this ADS to it.

    // Decrement the cap. TODO(siagraw)
    // Delete the ads cap. TODO(siagraw)
    return sel4test_get_result();
}
DEFINE_TEST(GPIADS005, "Ensure the threads with isolated stack works", test_ads_stack_isolated, true)
#endif

/*
    shallow_copy at a lib level.
    1. Get all the MO from the ads.
    2. Allocate a new MO.
    3. Copy the data from the old MO to the new MO.
        a. For this both need to be attached to current ads
    4. Attach the new MO to the new ads.
    5. Attach the new ads to the new thread.
*/