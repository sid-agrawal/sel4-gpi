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


vka_object_t ep_for_thread_normal;

void test_func(seL4_Word arg0, seL4_Word arg1, seL4_Word arg2) {

    // size_t tls_size = sel4runtime_get_tls_size();
    // seL4_Word initial_sp = arg0;
    // seL4_Word ipc_buffer_addr = arg1; 
    // uintptr_t tls_base = (uintptr_t)arg0 - tls_size;
    // uintptr_t tp = (uintptr_t)sel4runtime_write_tls_image((void *)tls_base);
    printf("Hello from test_func: ipc_buffer_add %p\n", arg0);
    //sel4runtime_set_tls_variable(tp, __sel4_ipc_buffer, arg0);
    seL4_SetIPCBuffer((seL4_IPCBuffer *)arg0);
    // error = seL4_TCB_SetTLSBase(env->tcb.cptr, tp);
    
    // printf("%s__sel4_ipc_buffer: %p\n", __FUNCTION__, __sel4_ipc_buffer); 


    ccnt_t end;
    SEL4BENCH_READ_CCNT(end);

    assert(ep_for_thread_normal.cptr != 0);
    uint64_t shared_var_stack;

    // Send IPC back to main thread.
        /* set the data to send. We smains_epend it in the first message register */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);

    seL4_SetMR(0, end);
    tag = seL4_Call(ep_for_thread_normal.cptr, tag);

    seL4_Word msg = seL4_GetMR(0);
    printf("new_thread: got a reply: %lu\n", msg);

    while(1);
}
    

int test_cpu_normal_therad(env_t env)
{
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
    printf("ADDRESS OF FUNC: %p\n", test_func);
    // Add args.
    error = cpu_client_start(&cpu_conn, (sel4utils_thread_entry_fn)test_func);
    test_error_eq(error, 0);

    printf("%d: main_thread: shared_var(%p) = %ld\n", __LINE__, &shared_var_stack, shared_var_stack);
        
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    seL4_Word msg;

    tag = seL4_Recv(ep_for_thread_normal.cptr, NULL);
    ZF_LOGF_IF(seL4_MessageInfo_get_length(tag) != 1,
               "Response data from the new process was not the length expected.\n"
               "\tHow many registers did you set with seL4_SetMR within the new process?\n");


    /* get the message stored in the first message register */
    ccnt_t   end = seL4_GetMR(0);
    printf("root-task: \tStart: %010ld\n\t, End: %ld\n\t, Diff: %ld\n",
           start, end, end - start);

    /* modify the message */
    seL4_SetMR(0, ~msg);
    

    printf("%d: main_thread: shared_var(%p) = %d\n", __LINE__, &shared_var_stack, shared_var_stack);
    

    // Send a message to the thread.

    return sel4test_get_result();
}
DEFINE_TEST(GPICPU001, "Ensure that normal thread works", test_cpu_normal_therad, true)
