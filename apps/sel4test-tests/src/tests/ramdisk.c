
#include <stdio.h>

#include <sel4test/test.h>
#include <sel4test/macros.h>
#include <sel4utils/thread.h>
#include <sel4gpi/debug.h>
#include "../test.h"
#include "../helpers.h"
#include <sel4bench/arch/sel4bench.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/pd_obj.h>

#include <ramdisk_client.h>
#include <ramdisk_server.h>

#define TEST_STR_1 "Fuzzy Wuzzy was a bear"
#define TEST_STR_2 "Fuzzy Wuzzy had no hair"
#define RAMDISK_APP "ramdisk_server"

#define DSB() asm volatile("dsb sy" ::: "memory")

static void flush_caches() {
    DSB();
    //seL4_BenchmarkFlushCaches();
}

/**
 * Starts the ramdisk as a process
 */
int start_ramdisk_pd(env_t env, seL4_CPtr *ramdisk_ep)
{
    int error;

    sel4bench_init();

    // Make new PD i.e. CSspace
    ccnt_t start;
    SEL4BENCH_READ_CCNT(start);

    /* create an endpoint for the parent to listen on*/
    vka_object_t ep_object = {0};
    error = vka_alloc_endpoint(&env->vka, &ep_object);
    test_assert(error == 0);

    /* Create a new PD */
    pd_client_context_t pd_os_cap;
    error = pd_component_client_connect(env->gpi_endpoint, &env->vka, &pd_os_cap);
    test_assert(error == 0);

     /* Create a new ADS Cap, which will be in the context of a PD and image */
    ads_client_context_t ads_os_cap;
    error = ads_component_client_connect(env->gpi_endpoint, &env->vka, &ads_os_cap);
    assert(error == 0);

    /*
        Give the PD some RDEs
        {
            "VA": "slot",
            "vCPU": "slot",
            ...
        }
    */

   // Make a new AS, loads an image
    error = pd_client_load(&pd_os_cap, &ads_os_cap, RAMDISK_APP);
    assert(error == 0);

    // Copy the parent ep to the new PD
    seL4_Word parent_ep_slot;
    printf("Sending parent ep\n");
    error = pd_client_send_cap(&pd_os_cap, ep_object.cptr, &parent_ep_slot);
    test_assert(error == 0);

    // Start it
    error = pd_client_start(&pd_os_cap, parent_ep_slot); // with this arg.
    test_assert(error == 0);

    // Wait for it to finish starting
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);

    /* Alloc cap receive path*/
    cspacepath_t received_cap_path;
    error = vka_cspace_alloc_path(&env->vka, &received_cap_path);
    test_assert(error == 0);

    seL4_SetCapReceivePath(received_cap_path.root,
                           received_cap_path.capPtr,
                           received_cap_path.capDepth);

    tag = seL4_Recv(ep_object.cptr, NULL);
    test_assert(seL4_MessageInfo_get_extraCaps(tag) == 1);
    test_assert(received_cap_path.capPtr != 1);
    *ramdisk_ep = received_cap_path.capPtr;

    printf("Received ep from ramdisk\n");
    return sel4test_get_result();
}

/**
 * Starts the ramdisk as a thread
 */

int start_ramdisk_thread(env_t env, seL4_CPtr *ramdisk_ep)
{

    int error;

    /* create an endpoint for the parent to listen on*/
    vka_object_t ep_object = {0};
    error = vka_alloc_endpoint(&env->vka, &ep_object);
    test_assert(error == 0);

    printf("Starting ramdisk thread\n");

    /* start ramdisk thread */
    error = ramdisk_server_spawn_thread(&env->simple,
                            &env->vka,
                            &env->vspace,
                            env->gpi_endpoint,
                            ep_object.cptr,
                            env->self_ads_cptr,
                            250);

    test_assert(error == 0);

    /* Wait for message from thread */
    cspacepath_t received_cap_path;
    error = vka_cspace_alloc_path(&env->vka, &received_cap_path);
    test_assert(error == 0);

    seL4_SetCapReceivePath(received_cap_path.root,
                           received_cap_path.capPtr,
                           received_cap_path.capDepth);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(ep_object.cptr, NULL);
    test_assert(seL4_MessageInfo_get_extraCaps(tag) == 1);
    test_assert(received_cap_path.capPtr != 0);
    *ramdisk_ep = received_cap_path.capPtr;

    printf("Received ep from ramdisk\n");
    return sel4test_get_result();
}

/**
 * Uses the ramdisk already started by the root task
*/
int use_rt_ramdisk_thread(env_t env, seL4_CPtr *ramdisk_ep)
{
    *ramdisk_ep = env->ramdisk_endpoint;
    return 0;
}

int test_ramdisk(env_t env)
{
    seL4_Error error;
    char *buf;

    printf("------------------STARTING SETUP: %s------------------\n", __func__);

    /* Initialize the ADS */
    ads_client_context_t ads_conn;
    vka_cspace_make_path(&env->vka, env->self_ads_cptr, &ads_conn.badged_server_ep_cspath);
    test_assert(error == 0);

    /* Create a memory object for the buffer */
    seL4_CPtr slot;
    vka_cspace_alloc(&env->vka, &slot);

    mo_client_context_t mo_conn;
    error = mo_component_client_connect(env->gpi_endpoint,
                                        slot,
                                        1,
                                        &mo_conn);
    test_assert(error == 0);
    printf("Finished mo_component_client_connect\n");

    error = ads_client_attach(&ads_conn,
                              NULL,
                              &mo_conn,
                              (void **)&buf);
    test_assert(error == 0);

    /* Temp test: mapping actually works */
    /*
    char *buf2;
    error = ads_client_attach(&ads_conn,
                              NULL,
                              &mo_conn,
                              (void **)&buf2);
    test_assert(error == 0);

    strcpy(buf, TEST_STR_1);
    test_assert(strcmp(buf, buf2) == 0);
    */
    //strcpy(buf, TEST_STR_1);

    /* Start ramdisk server process */
    seL4_CPtr ramdisk_ep;
    error = start_ramdisk_pd(env, &ramdisk_ep);
    test_assert(error == 0);

    printf("------------------STARTING TESTS: %s------------------\n", __func__);

    // Get a block
    ramdisk_client_context_t block;
    error = ramdisk_client_alloc_block(ramdisk_ep, &env->vka, &block);
    test_assert(error == seL4_NoError);

    // Write and read from beginning of disk
    strcpy(buf, TEST_STR_1);
    flush_caches();
    printf("buf addr: %p, contents: %s\n", buf, buf);
    error = ramdisk_client_write(&block, &mo_conn);
    test_assert(error == seL4_NoError);

    memset(buf, 0, RAMDISK_BLOCK_SIZE); // clear the test string from the buffer
    flush_caches();
    error = ramdisk_client_read(&block, &mo_conn);
    test_assert(error == seL4_NoError);
    printf("Result from read: %s\n", buf);
    test_assert(strcmp(buf, TEST_STR_1) == 0);

    // Write and read from another block
    ramdisk_client_context_t block2;
    error = ramdisk_client_alloc_block(ramdisk_ep, &env->vka, &block2);
    test_assert(error == seL4_NoError);

    strcpy(buf, TEST_STR_2);
    error = ramdisk_client_write(&block2, &mo_conn);
    test_assert(error == seL4_NoError);

    memset(buf, 0, RAMDISK_BLOCK_SIZE); // clear the test string from the buffer
    error = ramdisk_client_read(&block2, &mo_conn);
    test_assert(error == seL4_NoError);
    test_assert(strcmp(buf, TEST_STR_2) == 0);

    // Write/read entire block
    memset(buf, 0x42, RAMDISK_BLOCK_SIZE);

    error = ramdisk_client_write(&block, &mo_conn);
    test_assert(error == seL4_NoError);

    memset(buf, 0, RAMDISK_BLOCK_SIZE); // clear the test string from the buffer
    error = ramdisk_client_read(&block, &mo_conn);
    test_assert(error == seL4_NoError);
    test_assert(buf[0] == 0x42);
    test_assert(buf[RAMDISK_BLOCK_SIZE - 1] == 0x42);

    // Allocate a number of blocks
    for (int i = 0; i < 20; i++)
    {
        error = ramdisk_client_alloc_block(ramdisk_ep, &env->vka, &block);
        test_assert(error == seL4_NoError);

        buf[0] = i;
        error = ramdisk_client_write(&block, &mo_conn);
        test_assert(error == seL4_NoError);

        buf[0] = 0;
        error = ramdisk_client_read(&block, &mo_conn);
        test_assert(error == seL4_NoError);
        test_assert(buf[0] == i);
    }

    // TODO: test freeing blocks, if implemented

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPIRD001, "Ensure that the ramdisk is functioning", test_ramdisk, true)