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
#include<sel4gpi/debug.h>

#include <sel4bench/arch/sel4bench.h>


vka_object_t ep_for_thread_normal;

void test_func(seL4_Word arg0, seL4_Word arg1, seL4_Word arg2) {

    ccnt_t ctx_start, ctx_end;
    ccnt_t creation_start, creation_end;
    SEL4BENCH_READ_CCNT(creation_end);

    seL4_SetIPCBuffer((seL4_IPCBuffer *)arg0);
    OSDB_PRINTF("Hello from test_func: ipc_buffer_add %p\n", arg0);
    assert(ep_for_thread_normal.cptr != 0);
    uint64_t shared_var_stack;

    // Send IPC back to main thread.
        /* set the data to send. We smains_epend it in the first message register */
    seL4_MessageInfo_t tag;
    tag = seL4_MessageInfo_new(0, 0, 0, 1);


    SEL4BENCH_READ_CCNT(ctx_start);
    tag = seL4_Call(ep_for_thread_normal.cptr, tag);
    SEL4BENCH_READ_CCNT(ctx_end);
    creation_start = seL4_GetMR(0);
    printf("test-func: Creationg Time : %lu cycles\n", (creation_end - creation_start)/1000);


        float data[1000];
    // printf("test_func_die: addr of var on other stack: %p\n", other_stack); 
    printf("test_func_die: Cross AS IPC Time\n");
    int count = 1000;
    int i = 0;
    for(int i = 0; i < count; i++) {

        tag = seL4_MessageInfo_new(0, 0, 0, 1);
        SEL4BENCH_READ_CCNT(ctx_start);
        tag = seL4_Call(ep_for_thread_normal.cptr, tag);
        SEL4BENCH_READ_CCNT(ctx_end);
        data[i] = (ctx_end - ctx_start)/2;
    }
    
    float mean, sd;
    calculateSD(data, &mean, &sd, 1, 99);
    printf("MEAN: %f, SD: %f \n", mean/1000, sd/1000);



    while(1);
}
    

int test_cpu_normal_thread(env_t env)
{
    printf("------------ STARTING TEST: %s ------------\n", __FUNCTION__);
    int error;
    cspacepath_t path;
    vka_cspace_make_path(&env->vka, env->self_ads_cptr, &path);
    ads_client_context_t ads_conn;
    ads_conn.badged_server_ep_cspath = path;


    sel4bench_init();
    // Make new PD i.e. CSspace
    ccnt_t start;
    SEL4BENCH_READ_CCNT(start);

    error = vka_alloc_endpoint(&env->vka, &ep_for_thread_normal);
    assert(error == 0);


    // This shared var is on the stack an hence should not work.
    volatile uint64_t shared_var_stack = 1;

    // Allocate new CPU
    cpu_client_context_t cpu_conn;
    error = cpu_component_client_connect(env->gpi_endpoint,
                                         &env->vka,
                                         &cpu_conn);
    test_error_eq(error, 0);

    // Config its ads and cspace
    error = cpu_client_config(&cpu_conn,
                              &ads_conn,
                              env->cspace_root,
                              env->endpoint); // Fault EP
    test_error_eq(error, 0);

    // Start it.
    // (XXX)dd args.
    error = cpu_client_start(&cpu_conn, (sel4utils_thread_entry_fn)test_func);
    test_error_eq(error, 0);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    seL4_Word msg;

    tag = seL4_Recv(ep_for_thread_normal.cptr, NULL);
    assert(seL4_MessageInfo_get_length(tag) == 1);


    /* Send the start time as a reply */
    seL4_SetMR(0, start);
    seL4_ReplyRecv(ep_for_thread_normal.cptr, tag, NULL);
    
    printf("------------ Phase 2: %s ------------\n", __FUNCTION__);
    while (1)
    {
        //  printf("main responding to other thread\n");
        seL4_ReplyRecv(ep_for_thread_normal.cptr, tag, NULL);
    }
    printf("------------ ENDING TEST: %s ------------\n", __FUNCTION__);

    // printf("%d: main_thread: shared_var(%p) = %d\n", __LINE__, &shared_var_stack, shared_var_stack);

    // Send a message to the thread.

    return sel4test_get_result();
}
DEFINE_TEST(GPICPU001, "Ensure that normal thread works", test_cpu_normal_thread, true)
