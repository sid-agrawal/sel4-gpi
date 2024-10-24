
#include <stdio.h>

#include <vka/capops.h>

#include <sel4test/test.h>
#include <sel4test/macros.h>
#include <sel4utils/thread.h>
#include <sel4gpi/debug.h>
#include "../test.h"
#include "../helpers.h"
#include "test_shared.h"
#include <sel4bench/arch/sel4bench.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/vmr_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/pd_utils.h>

#include <ramdisk_client.h>

#define TEST_STR_1 "Fuzzy Wuzzy was a bear"
#define TEST_STR_2 "Fuzzy Wuzzy had no hair"

int test_ramdisk(env_t env)
{
    seL4_Error error = 0;
    char *buf;

    printf("------------------STARTING SETUP: %s------------------\n", __func__);

    /* Initialize the ADS */
    seL4_CPtr vmr_rde = sel4gpi_get_bound_vmr_rde();

    /* Initialize the PD */
    pd_client_context_t pd_conn = sel4gpi_get_pd_conn();

    /* Create a memory object for the buffer */
    mo_client_context_t mo_conn;
    error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO),
                                        1,
                                        MO_PAGE_BITS,
                                        &mo_conn);
    test_assert(error == 0);
    printf("Finished mo_component_client_connect\n");

    /*
    (XXX) - Arya
    We can't attach the MO here, or the ramdisk won't be able to
    see values written to it. Why is that the case?
    */

    /* Start ramdisk server process */
    gpi_space_id_t ramdisk_id;
    pd_client_context_t ramdisk_pd;
    error = start_ramdisk_pd(&ramdisk_pd, &ramdisk_id);
    test_assert(error == 0);

    /* Add the ramdisk to local RD */
    seL4_CPtr ramdisk_client_ep = sel4gpi_get_rde(sel4gpi_get_resource_type_code(BLOCK_RESOURCE_TYPE_NAME));

    printf("------------------STARTING TESTS: %s------------------\n", __func__);

    /* Attach MO to test's ADS */
    error = vmr_client_attach_no_reserve(vmr_rde,
                                         NULL,
                                         &mo_conn,
                                         SEL4UTILS_RES_TYPE_SHARED_FRAMES,
                                         (void **)&buf);
    test_assert(error == 0);

    // Set up shared memory
    error = ramdisk_client_bind(ramdisk_client_ep, &mo_conn);
    test_assert(error == seL4_NoError);

    // Get a block
    ramdisk_client_context_t block;
    error = ramdisk_client_alloc_block(ramdisk_client_ep, &block);
    test_assert(error == seL4_NoError);

    // Write and read from beginning of disk
    strcpy(buf, TEST_STR_1);
    printf("buf addr: %p, contents: %s\n", buf, buf);
    error = ramdisk_client_write(&block);
    test_assert(error == seL4_NoError);

    memset(buf, 0, RAMDISK_BLOCK_SIZE); // clear the test string from the buffer
    error = ramdisk_client_read(&block);
    test_assert(error == seL4_NoError);
    test_assert(strcmp(buf, TEST_STR_1) == 0);

    // Write and read from another block
    ramdisk_client_context_t block2;
    error = ramdisk_client_alloc_block(ramdisk_client_ep, &block2);
    test_assert(error == seL4_NoError);

    strcpy(buf, TEST_STR_2);
    error = ramdisk_client_write(&block2);
    test_assert(error == seL4_NoError);

    memset(buf, 0, RAMDISK_BLOCK_SIZE); // clear the test string from the buffer
    error = ramdisk_client_read(&block2);
    test_assert(error == seL4_NoError);
    test_assert(strcmp(buf, TEST_STR_2) == 0);

    error = ramdisk_client_free_block(&block2);
    test_assert(error == seL4_NoError);

    // Write/read entire block
    memset(buf, 0x42, RAMDISK_BLOCK_SIZE);

    error = ramdisk_client_write(&block);
    test_assert(error == seL4_NoError);

    memset(buf, 0, RAMDISK_BLOCK_SIZE); // clear the test string from the buffer
    error = ramdisk_client_read(&block);
    test_assert(error == seL4_NoError);
    test_assert(buf[0] == 0x42);
    test_assert(buf[RAMDISK_BLOCK_SIZE - 1] == 0x42);

    error = ramdisk_client_free_block(&block);
    test_assert(error == seL4_NoError);

    // Allocate a number of blocks
    for (int i = 3; i <= 20; i++)
    {
        printf("----- Allocating block %d ---- \n", i);
        error = ramdisk_client_alloc_block(ramdisk_client_ep, &block);
        test_assert(error == seL4_NoError);

        buf[0] = i;
        error = ramdisk_client_write(&block);
        test_assert(error == seL4_NoError);

        buf[0] = 0;
        error = ramdisk_client_read(&block);
        test_assert(error == seL4_NoError);
        test_assert(buf[0] == i);

        error = ramdisk_client_free_block(&block);
        test_assert(error == seL4_NoError);
    }

    // Unbind shared memory
    error = ramdisk_client_unbind(ramdisk_client_ep);
    test_assert(error == seL4_NoError);

    // Print whole-pd model state
    extract_model(&pd_conn);

    /* Remove RDEs from test process so that it won't be cleaned up by recursive cleanup */
    error = pd_client_remove_rde(&pd_conn, sel4gpi_get_resource_type_code(BLOCK_RESOURCE_TYPE_NAME), BADGE_SPACE_ID_NULL);
    test_assert(error == 0);

    // Cleanup server
    test_error_eq(maybe_terminate_pd(&ramdisk_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIRD001, "Ensure that the ramdisk is functioning", test_ramdisk, true)