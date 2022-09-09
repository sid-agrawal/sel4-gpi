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

#include<sel4gpi/ads_clientapi.h>
#include<sel4gpi/cpu_clientapi.h>

#include <sel4bench/arch/sel4bench.h>

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

#define MRS(reg, v)  asm volatile("mrs %x0," reg : "=r"(v))
#define MSR(reg, v)                                \
    do {                                           \
        word_t _v = v;                             \
        asm volatile("msr " reg ",%x0" :: "r" (_v));\
    }while(0)


#define SYSTEM_WRITE_WORD(reg, v) MSR(reg, v)
#define SYSTEM_READ_WORD(reg, v)  MRS(reg, v)

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

int test_ads_clone(env_t env)
{
    int error;
    cspacepath_t path;
    vka_cspace_make_path(&env->vka, env->self_ads_cptr, &path);
    ads_client_context_t conn;
    conn.badged_server_ep_cspath = path;


    //arm_init_ccnt();
    sel4bench_init();

    // Using a known EP, get a new ads CAP.
    ads_client_context_t ads_conn_clone1;
    // uint64_t start = timestamp_sid();
    // error = ads_client_clone(&conn, &env->vka,  (void *) 0x10001000, &ads_conn_clone1);
    // test_error_eq(error, 0);

    // uint64_t end = timestamp_sid();
    /* calculate diff in ns */
    // uint64_t diff = (end - start);
    // printf("Time taken to clone: %llu cycles\n", diff);
    // printf("Time taken to clone: %llu million cycles\n", diff/(1000*1000));

    ads_client_context_t ads_conn_clone2;
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
    printf("Time taken to clone: %lu cycles\n", end - start);
    // error = ads_client_clone(&ads_conn_clone1, &env->vka,  (void *) 0x10001000, &ads_conn_clone2);
    test_error_eq(error, 0);
    

    return sel4test_get_result();
}

// DEFINE_TEST(GPIADS001, "Ensure that as clone works", test_ads_clone, true)


vka_object_t ep_for_thread;

void test_func_die(seL4_Word arg0, seL4_Word arg1, seL4_Word arg2) {
    
    // This is nasty hack to get the IPC buffers address for the thread.
    seL4_SetIPCBuffer(arg2);
    


    ccnt_t end;
    SEL4BENCH_READ_CCNT(end);

    assert(ep_for_thread.cptr != 0);
    uint64_t shared_var_stack;
    printf("test_func_die: addr of var on this stack: %p\n", &shared_var_stack);

    // Send IPC back to main thread.
        /* set the data to send. We smains_epend it in the first message register */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, end);
    seL4_SetMR(1, (seL4_Word) &shared_var_stack);
    tag = seL4_Call(ep_for_thread.cptr, tag);

    int *msg = seL4_GetMR(0);
    printf("new_thread: got a reply: %p\n", msg);
    printf("Will try and write to it\n");
    // *msg = 0xdeadbeef;
    while(1);
}
    

// DEFINE_TEST(GPIADS001, "Ensure the ads clone works", test_ads_clone, true)
int test_ads_stack_isolated_stack_die(env_t env)
{
    int error;
    cspacepath_t path;
    vka_cspace_make_path(&env->vka, env->self_ads_cptr, &path);
    ads_client_context_t conn;
    conn.badged_server_ep_cspath = path;


    sel4bench_init();
    // Make new PD i.e. CSspace
    ccnt_t start;
    SEL4BENCH_READ_CCNT(start);


    error = vka_alloc_endpoint(&env->vka, &ep_for_thread);
    assert(error == 0);


    // This shared var is on the stack an hence should not work.
    volatile uint64_t shared_var_stack = 1;

    // Using a known EP, get a new ads CAP.
    ads_client_context_t ads_conn_clone1;
    error = ads_client_clone(&conn, &env->vka,  (void *) 0x10001000, &ads_conn_clone1);
    test_error_eq(error, 0);
    
    // TODO: Attach a new stack, this is done inside clinet_config for now.
    // stack_cap
    // ads_client_attach()
    // Attach a new stack

    // Allocate new CPU
    cpu_client_context_t cpu_conn;
    error = cpu_component_client_connect(env->gpi_endpoint,
                                         &env->vka,
                                         &cpu_conn);
    test_error_eq(error, 0);

    // Config its ads and cspace
    error = cpu_client_config(&cpu_conn,
                              &ads_conn_clone1,
                              env->cspace_root);
    test_error_eq(error, 0);

    // Start it.
    printf("ADDRESS OF FUNC: %p\n", test_func_die);
    // Add args.
    error = cpu_client_start(&cpu_conn, (sel4utils_thread_entry_fn)test_func_die);
    test_error_eq(error, 0);

    printf("%d: main_thread: shared_var(%p) = %ld\n", __LINE__, &shared_var_stack, shared_var_stack);
        
    shared_var_stack = 4;
     
    //  uint64_t *other_thread_stack = (uintptr_t*)0x10022fb8;
    //  *other_thread_stack = 5;

    // Wait for test_func_die to reply.

        /* Wait for the thread to finish */

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    seL4_Word msg;

    tag = seL4_Recv(ep_for_thread.cptr, NULL);
    ZF_LOGF_IF(seL4_MessageInfo_get_length(tag) != 2,
               "Response data from the new process was not the length expected.\n"
               "\tHow many registers did you set with seL4_SetMR within the new process?\n");


    /* get the message stored in the first message register */
    ccnt_t   end = seL4_GetMR(0);
    printf("root-task: \tStart: %010ld\n\t, End: %ld\n\t, Diff: %ld\n",
           start, end, end - start);

    uint64_t *other_thread_stack = (uintptr_t*)seL4_GetMR(1);
    printf("root-task: \t Writing to Other thread's stack: %p\n", other_thread_stack);
   // *other_thread_stack = 5;
    /* modify the message */
    seL4_Word main_thread_stack = 5;
    seL4_SetMR(0, (seL4_Word) &main_thread_stack);
    seL4_ReplyRecv(ep_for_thread.cptr, tag, NULL);
    

    // printf("%d: main_thread: shared_var(%p) = %d\n", __LINE__, &shared_var_stack, shared_var_stack);
    

    // Send a message to the thread.
    seL4_DebugDumpScheduler();

    return sel4test_get_result();
}
DEFINE_TEST(GPIADS002, "Ensure that thread stack works", test_ads_stack_isolated_stack_die, true)

#ifdef WQEAREREADY

DEFINE_TEST(GPIADS002, "Ensure that new process create works", test_new_process, true)
int test_ads_attach(env_t env)
{
    ads_client_context_t conn;
    // Using a known EP, get a new ads CAP.
    int error = ads_component_client_connect(env->ads_endpoint, &env->vka, &conn);
    test_error_eq(error, 0);

    // Increment the ads cap.
    error = ads_client_attach(&conn, 0, 0, 0);
    test_error_eq(error, 0);

    // Decrement the cap. TODO(siagraw)
    // Delete the ads cap. TODO(siagraw)
    return sel4test_get_result();
}
DEFINE_TEST(GPIADS002, "Ensure the ads attach works", test_ads_attach, true)

int test_ads_bind_cpu(env_t env)
{
    ads_client_context_t conn;
    // Using a known EP, get a new ads CAP.
    int error = ads_component_client_connect(env->ads_endpoint, &env->vka, &conn);
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

int test_ads_clone(env_t env)
{
    ads_client_context_t conn;
    // Using a known EP, get a new ads CAP.
    int error = ads_component_client_connect(env->ads_endpoint, &env->vka, &conn);
    test_error_eq(error, 0);

    // Increment the ads cap.
    error = 0 ;// Call clone
    test_error_eq(error, 0);

    // Decrement the cap. TODO(siagraw)
    // Delete the ads cap. TODO(siagraw)
    return sel4test_get_result();
}
DEFINE_TEST(GPIADS004, "Ensure the ads clone works", test_ads_clone, true)

int test_ads_stack_isolated(env_t env)
{
    ads_client_context_t conn;
    // Using a known EP, get a new ads CAP.
    int error = ads_component_client_connect(env->ads_endpoint, &env->vka, &conn);
    test_error_eq(error, 0);

    // Clone the ads,
    // ads_client_clone(&conn, 0, 0, 0);
    // Attach a new stack
    // stack_cap
    // ads_client_clone(&conn, 0, 0, 0);
    // Attach a new stack
    // Allocate a new PD i.e. cspace.
    // Allocate a new TCB and attach this ADS to it.

    // Decrement the cap. TODO(siagraw)
    // Delete the ads cap. TODO(siagraw)
    return sel4test_get_result();
}
DEFINE_TEST(GPIADS005, "Ensure the threads with isolated stack works", test_ads_stack_isolated, true)

#endif

