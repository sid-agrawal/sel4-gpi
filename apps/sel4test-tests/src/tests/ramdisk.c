
#include <stdio.h>

#include <sel4test/test.h>
#include <sel4test/macros.h>
#include "../test.h"
#include "../helpers.h"

#include <ramdisk/ramdisk.h>

#include <sel4gpi/gpi_server.h>
#include <sel4gpi/ads_clientapi.h>

#define TEST_STR_1 "Fuzzy Wuzzy was a bear"
#define TEST_STR_2 "Fuzzy Wuzzy had no hair"

int test_ramdisk(env_t env)
{
    seL4_Error error;
    char *buf;

    printf("------------------STARTING SETUP: %s------------------\n", __func__);

    /* Initialize the ADS */
    /*
    ads_client_context_t ads_conn;
    error = ads_component_client_connect(env->gpi_endpoint,
                                         &env->vka,
                                         &ads_conn);
    test_assert(error == 0);
    printf("Finished ads_component_client_connect\n");
    */
    ads_client_context_t ads_conn;
    vka_cspace_make_path(&env->vka, env->self_ads_cptr, &ads_conn.badged_server_ep_cspath);
    test_assert(error == 0);

    /* Create a memory object for the buffer */
    mo_client_context_t mo_conn;
    error = mo_component_client_connect(env->gpi_endpoint,
                                        &env->vka,
                                        1,
                                        &mo_conn);
    test_assert(error == 0);
    printf("Finished mo_component_client_connect\n");

    error = ads_client_attach(&ads_conn,
                              NULL,
                              &mo_conn,
                              (void **)&buf);
    test_assert(error == 0);

    printf("------------------STARTING TESTS: %s------------------\n", __func__);

    // Get a block
    ramdisk_client_context_t block;
    error = ramdisk_client_alloc_block(env->ramdisk_endpoint, &env->vka, &block);
    test_assert(error == seL4_NoError);

    // Write and read from beginning of disk
    strcpy(buf, TEST_STR_1);
    error = ramdisk_client_write(&block, &mo_conn);
    test_assert(error == seL4_NoError);

    memset(buf, 0, RAMDISK_BLOCK_SIZE); // clear the test string from the buffer
    error = ramdisk_client_read(&block, &mo_conn);
    test_assert(error == seL4_NoError);
    test_assert(strcmp(buf, TEST_STR_1) == 0);

    // Write and read from another block
    ramdisk_client_context_t block2;
    error = ramdisk_client_alloc_block(env->ramdisk_endpoint, &env->vka, &block2);
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
    for (int i = 0; i < 20; i++) {
        error = ramdisk_client_alloc_block(env->ramdisk_endpoint, &env->vka, &block);
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