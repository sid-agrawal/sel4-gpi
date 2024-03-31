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

#define TEST_LOG(msg, ...)                                  \
    do                                                      \
    {                                                       \
        printf("%s(): " msg "\n", __func__, ##__VA_ARGS__); \
    } while (0)

int test_native_benchmarks(env_t env)
{
    sel4bench_init();
    int error;
    ccnt_t pd_create_start_time;
    SEL4BENCH_READ_CCNT(pd_create_start_time);

    sel4utils_process_t proc;
    cspacepath_t asid_cap_init, asid_cap_env;
    vka_cspace_make_path(&env->vka, seL4_CapInitThreadASIDPool, &asid_cap_init);
    vka_cspace_make_path(&env->vka, env->asid_pool, &asid_cap_env);
    error = vka_cnode_copy(&asid_cap_init, &asid_cap_env, seL4_AllRights);
    test_error_eq(error, 0);

    error = sel4utils_configure_process(&proc, &env->vka, &env->vspace, "hello-native");
    test_error_eq(error, 0);

    vka_object_t ep;
    error = vka_alloc_endpoint(&env->vka, &ep);
    test_error_eq(error, 0);

    // cspacepath_t src, dest;
    // vka_cspace_make_path(&env->vka, ep.cptr, &src);
    seL4_CPtr ep_slot = sel4utils_copy_cap_to_process(&proc, &env->vka, ep.cptr);

    seL4_Word argc = 1;
    char string_args[argc][WORD_STRING_SIZE];
    char *argv[argc];
    sel4utils_create_word_args(string_args, argv, argc, ep_slot);

    error = sel4utils_spawn_process_v(&proc, &env->vka, &env->vspace,
                                      argc, argv, 1);
    test_error_eq(error, 0);

    seL4_DebugNameThread(proc.thread.tcb.cptr, "hello-native");

    // seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    // cspacepath_t ipc_cap;
    // error = vka_cspace_alloc_path(&env->vka, &ipc_cap);
    // test_error_eq(error, 0);

    // seL4_SetCapReceivePath(ipc_cap.root, ipc_cap.capPtr, ipc_cap.capDepth);
    seL4_MessageInfo_t tag = seL4_Recv(ep.cptr, NULL);
    seL4_Word pd_create_end_time = seL4_GetMR(0);

    // test_assert(ipc_cap.capPtr != seL4_CapNull);

    // tag = seL4_MessageInfo_new(0, 0, 0, 1);
    // seL4_SetMR(0, pd_create_start_time);
    // seL4_Send(ipc_cap.capPtr, tag);
    TEST_LOG("NATIVE PD CREATE TIME: %ld (start: %ld, end: %ld)", pd_create_end_time - pd_create_start_time, pd_create_start_time, pd_create_end_time);

    sel4bench_destroy();
    return sel4test_get_result();
}
DEFINE_TEST(GPIBM001,
            "Native seL4 Benchmark",
            test_native_benchmarks,
            true)