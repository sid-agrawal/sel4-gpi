
#include <stdio.h>

#include <vka/capops.h>

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
#define FAKE_CLIENT_ID 1

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

    /* Initialize the PD */
    pd_client_context_t pd_conn;
    vka_cspace_make_path(&env->vka, env->self_pd_cptr, &pd_conn.badged_server_ep_cspath);
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

    /*
    (XXX) - Arya
    We can't attach the MO here, or the ramdisk won't be able to
    see values written to it. Why is that the case?
    */

    /* Start ramdisk server process */
    seL4_CPtr ramdisk_ep;
    seL4_CPtr ramdisk_pd_cap;
    error = start_ramdisk_pd(&env->vka, env->gpi_endpoint, &ramdisk_ep, &ramdisk_pd_cap);
    test_assert(error == 0);

    /* Badge the ramdisk EP with a client ID to simulate being a client */
    cspacepath_t src, dest;
    seL4_CPtr ramdisk_client_ep;
    vka_cspace_make_path(&env->vka, ramdisk_ep, &src);

    error = vka_cspace_alloc_path(&env->vka, &dest);
    test_assert(error == 0);

    seL4_Word badge_val = gpi_new_badge(GPICAP_TYPE_BLOCK,
                                        0x00,
                                        FAKE_CLIENT_ID,
                                        BADGE_OBJ_ID_NULL);

    error = vka_cnode_mint(&dest,
                           &src,
                           seL4_AllRights,
                           badge_val);
    test_assert(error == 0);
    ramdisk_client_ep = dest.capPtr;

    printf("------------------STARTING TESTS: %s------------------\n", __func__);

    /* Attach MO to test's ADS */
    error = ads_client_attach(&ads_conn,
                              NULL,
                              &mo_conn,
                              (void **)&buf);
    test_assert(error == 0);

    /* Sanity test shared memory */
    seL4_Word test_value = 0x1234;
    *((int *)buf) = test_value;
    printf("buf addr: %p, contents: 0x%x\n", buf, *((int *)buf));
    seL4_Word res;
    error = ramdisk_client_sanity_test(ramdisk_client_ep, &mo_conn, &res);
    test_assert(error == seL4_NoError);
    test_assert(res == test_value);

    // Get a block
    ramdisk_client_context_t block;
    error = ramdisk_client_alloc_block(ramdisk_client_ep, &env->vka, 0, &block, NULL);
    test_assert(error == seL4_NoError);

    // Write and read from beginning of disk
    strcpy(buf, TEST_STR_1);
    printf("buf addr: %p, contents: %s\n", buf, buf);
    error = ramdisk_client_write(&block, &mo_conn);
    test_assert(error == seL4_NoError);

    memset(buf, 0, RAMDISK_BLOCK_SIZE); // clear the test string from the buffer
    error = ramdisk_client_read(&block, &mo_conn);
    test_assert(error == seL4_NoError);
    test_assert(strcmp(buf, TEST_STR_1) == 0);

    // Write and read from another block
    ramdisk_client_context_t block2;
    error = ramdisk_client_alloc_block(ramdisk_client_ep, &env->vka, 0, &block2, NULL);
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
    for (int i = 3; i <= 20; i++)
    {
        printf("----- Allocating block %d ---- \n", i);
        error = ramdisk_client_alloc_block(ramdisk_client_ep, &env->vka, 0, &block, NULL);
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

    // Print whole-pd model state
    error = pd_client_dump(&pd_conn, NULL, 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPIRD001, "Ensure that the ramdisk is functioning", test_ramdisk, true)