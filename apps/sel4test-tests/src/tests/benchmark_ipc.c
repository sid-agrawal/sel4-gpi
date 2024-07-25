#include <stdio.h>
#include <sel4/sel4.h>
#include <sel4test/test.h>
#include <sel4test/macros.h>
#include <sel4utils/thread.h>
#include <vka/capops.h>
#include <sel4utils/thread.h>
#include <sel4bench/arch/sel4bench.h>
#include <utils/uthash.h>
#include <sel4rpc/client.h>
#include <rpc.pb.h>

#include <sel4gpi/bench_utils.h>

#include "../test.h"
#include "../helpers.h"
#include "test_shared.h"

/**
 * Benchmark RTT of regular IPCs to the sel4test driver
 * Parameters configure the outgoing message, but the return message will always be the same
 * (no caps, short message), to represent the messages sent within CellulOS
 *
 * @param env
 * @param n_iters number of test iterations to run
 * @param long_msg if true, send a long outgoing message (512 bytes)
 *                 if false, send a short outgoing message (64 bytes)
 * @param caps caps to send with each outgoing message
 * @param n_caps number of caps to send
 */
static int internal_benchmark_regular_ipc(env_t env, int n_iters, int long_msg, seL4_CPtr *caps, int n_caps)
{
    int error = 0;
    ccnt_t call_start;
    ccnt_t call_end;
    int long_msg_len = 8;

    benchmark_init(env);

    for (int i = 0; i < n_iters; i++)
    {
        SEL4BENCH_READ_CCNT(call_start);
        // Include time to write data to IPC buf, since the NanoPB functions include it
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, n_caps, long_msg ? 1 + long_msg_len : 1);
        seL4_SetMR(0, SEL4TEST_BENCH_IPC);

        if (long_msg)
        {
            for (int i = 1; i < long_msg_len + 1; i++)
            {
                seL4_SetMR(i, 0x4242424242424242);
            }
        }

        for (int i = 0; i < n_caps; i++)
        {
            seL4_SetCap(i, caps[i]);
        }

        tag = seL4_Call(env->endpoint, tag);
        SEL4BENCH_READ_CCNT(call_end);

        test_error_eq(seL4_MessageInfo_ptr_get_label(&tag), 0);

        benchmark_print_result(call_end - call_start);
    }

    sel4bench_destroy();
    return sel4test_get_result();
}

/**
 * Benchmark RTT of IPCs to the sel4test driver using NanoPB
 * Parameters configure the outgoing message, but the return message will always be the same
 * (no caps, short message), to represent the messages sent within CellulOS
 *
 * @param env
 * @param n_iters number of test iterations to run
 * @param long_msg if true, send a long outgoing message (512 bytes)
 *                 if false, send a short outgoing message (64 bytes)
 * @param caps caps to send with each outgoing message
 * @param n_caps number of caps to send
 */
static int internal_benchmark_nanopb_ipc(env_t env, int n_iters, int long_msg, seL4_CPtr *caps, int n_caps)
{
    int error = 0;
    ccnt_t call_start;
    ccnt_t call_end;
    
    benchmark_init(env);

    for (int i = 0; i < n_iters; i++)
    {
        RpcMessage rpcMsg;

        if (long_msg)
        {
            rpcMsg.which_msg = RpcMessage_bench_long_tag;
            memset(rpcMsg.msg.bench_long.args, 0x42, sizeof(rpcMsg.msg.bench_long.args));
        }
        else
        {
            rpcMsg.which_msg = RpcMessage_bench_tag;
        }

        SEL4BENCH_READ_CCNT(call_start);
        // (XXX) Arya: does not do cap transfer at the moment
        error = sel4rpc_call_with_caps(&env->rpc_client, &rpcMsg, 0, 0, 0, caps, n_caps);
        SEL4BENCH_READ_CCNT(call_end);
        test_error_eq(error, 0);

        benchmark_print_result(call_end - call_start);
    }

    sel4bench_destroy();
    return sel4test_get_result();
}

int benchmark_regular_ipc_nocap_short(env_t env)
{
    return internal_benchmark_regular_ipc(env, 1, 0, NULL, 0);
}

int benchmark_regular_ipc_nocap_long(env_t env)
{
    return internal_benchmark_regular_ipc(env, 1, 1, NULL, 0);
}

int benchmark_regular_ipc_cap_short(env_t env)
{
    seL4_CPtr caps[1] = {env->cspace_root};
    return internal_benchmark_regular_ipc(env, 1, 0, caps, 1);
}

int benchmark_regular_ipc_cap_long(env_t env)
{
    seL4_CPtr caps[1] = {env->cspace_root};
    return internal_benchmark_regular_ipc(env, 1, 1, caps, 1);
}

// Create a badged version of the driver endpoint
static int make_badged_endpoint(env_t env, seL4_CPtr *result)
{
    int error = 0;
    cspacepath_t src, dest;

    vka_cspace_make_path(&env->vka, env->endpoint, &src);
    error = vka_cspace_alloc_path(&env->vka, &dest);
    test_error_eq(error, 0);

    error = vka_cnode_mint(&dest, &src, seL4_AllRights, 0x42);
    test_error_eq(error, 0);

    *result = dest.capPtr;

    return error;
}

int benchmark_regular_ipc_nocap_unwrapped_short(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep;
    test_error_eq(make_badged_endpoint(env, &badged_ep), 0);

    seL4_CPtr caps[1] = {badged_ep};
    return internal_benchmark_regular_ipc(env, 1, 0, caps, 1);
}

int benchmark_regular_ipc_nocap_unwrapped_long(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep;
    test_error_eq(make_badged_endpoint(env, &badged_ep), 0);

    seL4_CPtr caps[1] = {badged_ep};
    return internal_benchmark_regular_ipc(env, 1, 1, caps, 1);
}

int benchmark_regular_ipc_cap_unwrapped_short(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep;
    test_error_eq(make_badged_endpoint(env, &badged_ep), 0);

    seL4_CPtr caps[2] = {env->cspace_root, badged_ep};
    return internal_benchmark_regular_ipc(env, 1, 0, caps, 2);
}

int benchmark_regular_ipc_cap_unwrapped_long(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep;
    test_error_eq(make_badged_endpoint(env, &badged_ep), 0);

    seL4_CPtr caps[2] = {env->cspace_root, badged_ep};
    return internal_benchmark_regular_ipc(env, 1, 1, caps, 2);
}

int benchmark_regular_ipc_nocap_2_unwrapped_short(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep_1, badged_ep_2;
    test_error_eq(make_badged_endpoint(env, &badged_ep_1), 0);
    test_error_eq(make_badged_endpoint(env, &badged_ep_2), 0);

    seL4_CPtr caps[2] = {badged_ep_1, badged_ep_2};
    return internal_benchmark_regular_ipc(env, 1, 0, caps, 2);
}

int benchmark_regular_ipc_nocap_2_unwrapped_long(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep_1, badged_ep_2;
    test_error_eq(make_badged_endpoint(env, &badged_ep_1), 0);
    test_error_eq(make_badged_endpoint(env, &badged_ep_2), 0);

    seL4_CPtr caps[2] = {badged_ep_1, badged_ep_2};
    return internal_benchmark_regular_ipc(env, 1, 0, caps, 2);
}

int benchmark_regular_ipc_cap_2_unwrapped_short(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep_1, badged_ep_2;
    test_error_eq(make_badged_endpoint(env, &badged_ep_1), 0);
    test_error_eq(make_badged_endpoint(env, &badged_ep_2), 0);

    seL4_CPtr caps[3] = {env->cspace_root, badged_ep_1, badged_ep_2};
    return internal_benchmark_regular_ipc(env, 1, 0, caps, 3);
}

int benchmark_regular_ipc_cap_2_unwrapped_long(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep_1, badged_ep_2;
    test_error_eq(make_badged_endpoint(env, &badged_ep_1), 0);
    test_error_eq(make_badged_endpoint(env, &badged_ep_2), 0);

    seL4_CPtr caps[3] = {env->cspace_root, badged_ep_1, badged_ep_2};
    return internal_benchmark_regular_ipc(env, 1, 0, caps, 3);
}

int benchmark_nanopb_ipc_nocap_short(env_t env)
{
    return internal_benchmark_nanopb_ipc(env, 1, 0, NULL, 0);
}

int benchmark_nanopb_ipc_nocap_long(env_t env)
{
    return internal_benchmark_nanopb_ipc(env, 1, 1, NULL, 0);
}

int benchmark_nanopb_ipc_cap_short(env_t env)
{
    seL4_CPtr caps[1] = {env->cspace_root};
    return internal_benchmark_nanopb_ipc(env, 1, 0, caps, 1);
}

int benchmark_nanopb_ipc_cap_long(env_t env)
{
    seL4_CPtr caps[1] = {env->cspace_root};
    return internal_benchmark_nanopb_ipc(env, 1, 1, caps, 1);
}

int benchmark_nanopb_ipc_nocap_unwrapped_short(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep;
    test_error_eq(make_badged_endpoint(env, &badged_ep), 0);

    seL4_CPtr caps[1] = {badged_ep};
    return internal_benchmark_nanopb_ipc(env, 1, 0, caps, 1);
}

int benchmark_nanopb_ipc_nocap_unwrapped_long(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep;
    test_error_eq(make_badged_endpoint(env, &badged_ep), 0);

    seL4_CPtr caps[1] = {badged_ep};
    return internal_benchmark_nanopb_ipc(env, 1, 1, caps, 1);
}

int benchmark_nanopb_ipc_cap_unwrapped_short(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep;
    test_error_eq(make_badged_endpoint(env, &badged_ep), 0);

    seL4_CPtr caps[2] = {env->cspace_root, badged_ep};
    return internal_benchmark_nanopb_ipc(env, 1, 0, caps, 2);
}

int benchmark_nanopb_ipc_cap_unwrapped_long(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep;
    test_error_eq(make_badged_endpoint(env, &badged_ep), 0);

    seL4_CPtr caps[2] = {env->cspace_root, badged_ep};
    return internal_benchmark_nanopb_ipc(env, 1, 1, caps, 2);
}

int benchmark_nanopb_ipc_nocap_2_unwrapped_short(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep_1, badged_ep_2;
    test_error_eq(make_badged_endpoint(env, &badged_ep_1), 0);
    test_error_eq(make_badged_endpoint(env, &badged_ep_2), 0);

    seL4_CPtr caps[3] = {badged_ep_1, badged_ep_2};
    return internal_benchmark_nanopb_ipc(env, 1, 0, caps, 2);
}

int benchmark_nanopb_ipc_nocap_2_unwrapped_long(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep_1, badged_ep_2;
    test_error_eq(make_badged_endpoint(env, &badged_ep_1), 0);
    test_error_eq(make_badged_endpoint(env, &badged_ep_2), 0);

    seL4_CPtr caps[2] = {badged_ep_1, badged_ep_2};
    return internal_benchmark_nanopb_ipc(env, 1, 0, caps, 2);
}

int benchmark_nanopb_ipc_cap_2_unwrapped_short(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep_1, badged_ep_2;
    test_error_eq(make_badged_endpoint(env, &badged_ep_1), 0);
    test_error_eq(make_badged_endpoint(env, &badged_ep_2), 0);

    seL4_CPtr caps[3] = {env->cspace_root, badged_ep_1, badged_ep_2};
    return internal_benchmark_nanopb_ipc(env, 1, 0, caps, 3);
}

int benchmark_nanopb_ipc_cap_2_unwrapped_long(env_t env)
{
    int error = 0;

    // Make a badged version of the driver ep to send
    seL4_CPtr badged_ep_1, badged_ep_2;
    test_error_eq(make_badged_endpoint(env, &badged_ep_1), 0);
    test_error_eq(make_badged_endpoint(env, &badged_ep_2), 0);

    seL4_CPtr caps[3] = {env->cspace_root, badged_ep_1, badged_ep_2};
    return internal_benchmark_nanopb_ipc(env, 1, 0, caps, 3);
}

// (XXX) Arya: TODO test many IPC in a row
#if 0
int benchmark_many_regular_ipc(env_t env)
{
    return internal_benchmark_regular_ipc(env, 50, 0, NULL, 0);
}

int benchmark_many_nanopb_ipc(env_t env)
{
    return internal_benchmark_nanopb_ipc(env, 50, 0, NULL, 0);
}
#endif

#ifdef GPI_BENCHMARK_MULTIPLE
DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM100,
                               "benchmark_regular_ipc_nocap_short",
                               benchmark_regular_ipc_nocap_short,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM101,
                               "benchmark_regular_ipc_nocap_long",
                               benchmark_regular_ipc_nocap_long,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM102,
                               "benchmark_regular_ipc_cap_short",
                               benchmark_regular_ipc_cap_short,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM103,
                               "benchmark_regular_ipc_cap_long",
                               benchmark_regular_ipc_cap_long,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM104,
                               "benchmark_regular_ipc_nocap_unwrapped_short",
                               benchmark_regular_ipc_nocap_unwrapped_short,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM105,
                               "benchmark_regular_ipc_nocap_unwrapped_long",
                               benchmark_regular_ipc_nocap_unwrapped_long,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM106,
                               "benchmark_regular_ipc_cap_unwrapped_short",
                               benchmark_regular_ipc_cap_unwrapped_short,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM107,
                               "benchmark_regular_ipc_cap_unwrapped_long",
                               benchmark_regular_ipc_cap_unwrapped_long,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM108,
                               "benchmark_regular_ipc_nocap_2_unwrapped_short",
                               benchmark_regular_ipc_nocap_2_unwrapped_short,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM109,
                               "benchmark_regular_ipc_nocap_2_unwrapped_long",
                               benchmark_regular_ipc_nocap_2_unwrapped_long,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM110,
                               "benchmark_regular_ipc_cap_2_unwrapped_short",
                               benchmark_regular_ipc_cap_2_unwrapped_short,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM111,
                               "benchmark_regular_ipc_cap_2_unwrapped_long",
                               benchmark_regular_ipc_cap_2_unwrapped_long,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM112,
                               "benchmark_nanopb_ipc_nocap_short",
                               benchmark_nanopb_ipc_nocap_short,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM113,
                               "benchmark_nanopb_ipc_nocap_long",
                               benchmark_nanopb_ipc_nocap_long,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM114,
                               "benchmark_nanopb_ipc_cap_short",
                               benchmark_nanopb_ipc_cap_short,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM115,
                               "benchmark_nanopb_ipc_cap_long",
                               benchmark_nanopb_ipc_cap_long,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM116,
                               "benchmark_nanopb_ipc_nocap_unwrapped_short",
                               benchmark_nanopb_ipc_nocap_unwrapped_short,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM117,
                               "benchmark_nanopb_ipc_nocap_unwrapped_long",
                               benchmark_nanopb_ipc_nocap_unwrapped_long,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM118,
                               "benchmark_nanopb_ipc_cap_unwrapped_short",
                               benchmark_nanopb_ipc_cap_unwrapped_short,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM119,
                               "benchmark_nanopb_ipc_cap_unwrapped_long",
                               benchmark_nanopb_ipc_cap_unwrapped_long,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM120,
                               "benchmark_nanopb_ipc_nocap_2_unwrapped_short",
                               benchmark_nanopb_ipc_nocap_2_unwrapped_short,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM121,
                               "benchmark_nanopb_ipc_nocap_2_unwrapped_long",
                               benchmark_nanopb_ipc_nocap_2_unwrapped_long,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM122,
                               "benchmark_nanopb_ipc_cap_2_unwrapped_short",
                               benchmark_nanopb_ipc_cap_2_unwrapped_short,
                               BASIC,
                               true)

DEFINE_TEST_WITH_TYPE_MULTIPLE(GPIBM123,
                               "benchmark_nanopb_ipc_cap_2_unwrapped_long",
                               benchmark_nanopb_ipc_cap_2_unwrapped_long,
                               BASIC,
                               true)

#else

DEFINE_TEST(GPIBM100,
            "benchmark_regular_ipc_nocap_short",
            benchmark_regular_ipc_nocap_short,
            true)

DEFINE_TEST(GPIBM101,
            "benchmark_regular_ipc_nocap_long",
            benchmark_regular_ipc_nocap_long,
            true)

DEFINE_TEST(GPIBM102,
            "benchmark_regular_ipc_cap_short",
            benchmark_regular_ipc_cap_short,
            true)

DEFINE_TEST(GPIBM103,
            "benchmark_regular_ipc_cap_long",
            benchmark_regular_ipc_cap_long,
            true)

DEFINE_TEST(GPIBM104,
            "benchmark_regular_ipc_nocap_unwrapped_short",
            benchmark_regular_ipc_nocap_unwrapped_short,
            true)

DEFINE_TEST(GPIBM105,
            "benchmark_regular_ipc_nocap_unwrapped_long",
            benchmark_regular_ipc_nocap_unwrapped_long,
            true)

DEFINE_TEST(GPIBM106,
            "benchmark_regular_ipc_cap_unwrapped_short",
            benchmark_regular_ipc_cap_unwrapped_short,
            true)

DEFINE_TEST(GPIBM107,
            "benchmark_regular_ipc_cap_unwrapped_long",
            benchmark_regular_ipc_cap_unwrapped_long,
            true)

DEFINE_TEST(GPIBM108,
            "benchmark_regular_ipc_nocap_2_unwrapped_short",
            benchmark_regular_ipc_nocap_2_unwrapped_short,
            true)

DEFINE_TEST(GPIBM109,
            "benchmark_regular_ipc_nocap_2_unwrapped_long",
            benchmark_regular_ipc_nocap_2_unwrapped_long,
            true)

DEFINE_TEST(GPIBM110,
            "benchmark_regular_ipc_cap_2_unwrapped_short",
            benchmark_regular_ipc_cap_2_unwrapped_short,
            true)

DEFINE_TEST(GPIBM111,
            "benchmark_regular_ipc_cap_2_unwrapped_long",
            benchmark_regular_ipc_cap_2_unwrapped_long,
            true)

DEFINE_TEST(GPIBM112,
            "benchmark_nanopb_ipc_nocap_short",
            benchmark_nanopb_ipc_nocap_short,
            true)

DEFINE_TEST(GPIBM113,
            "benchmark_nanopb_ipc_nocap_long",
            benchmark_nanopb_ipc_nocap_long,
            true)

DEFINE_TEST(GPIBM114,
            "benchmark_nanopb_ipc_cap_short",
            benchmark_nanopb_ipc_cap_short,
            true)

DEFINE_TEST(GPIBM115,
            "benchmark_nanopb_ipc_cap_long",
            benchmark_nanopb_ipc_cap_long,
            true)

DEFINE_TEST(GPIBM116,
            "benchmark_nanopb_ipc_nocap_unwrapped_short",
            benchmark_nanopb_ipc_nocap_unwrapped_short,
            true)

DEFINE_TEST(GPIBM117,
            "benchmark_nanopb_ipc_nocap_unwrapped_long",
            benchmark_nanopb_ipc_nocap_unwrapped_long,
            true)

DEFINE_TEST(GPIBM118,
            "benchmark_nanopb_ipc_cap_unwrapped_short",
            benchmark_nanopb_ipc_cap_unwrapped_short,
            true)

DEFINE_TEST(GPIBM119,
            "benchmark_nanopb_ipc_cap_unwrapped_long",
            benchmark_nanopb_ipc_cap_unwrapped_long,
            true)

DEFINE_TEST(GPIBM120,
            "benchmark_nanopb_ipc_nocap_2_unwrapped_short",
            benchmark_nanopb_ipc_nocap_2_unwrapped_short,
            true)

DEFINE_TEST(GPIBM121,
            "benchmark_nanopb_ipc_nocap_2_unwrapped_long",
            benchmark_nanopb_ipc_nocap_2_unwrapped_long,
            true)

DEFINE_TEST(GPIBM122,
            "benchmark_nanopb_ipc_cap_2_unwrapped_short",
            benchmark_nanopb_ipc_cap_2_unwrapped_short,
            true)

DEFINE_TEST(GPIBM123,
            "benchmark_nanopb_ipc_cap_2_unwrapped_long",
            benchmark_nanopb_ipc_cap_2_unwrapped_long,
            true)

#endif