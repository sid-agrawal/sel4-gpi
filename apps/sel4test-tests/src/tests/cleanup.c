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
#include <sel4gpi/cpu_clientapi.h>
#include <sel4bench/arch/sel4bench.h>
#include <utils/uthash.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/pd_creation.h>
#include <sel4gpi/resource_server_clientapi.h>

#include "test_shared.h"

/**
 * Test the PD cleanup policies
 *
 * This cannot be tested programmatically
 * This test just creates a test scenario and outputs the model state after a PD has crashed
 * User will have to ensure the output model state looks correct
 *
 * The cleanup policy is currently set by the defined GPI_CLEANUP_POLICY
 */

#define HELLO_CLEANUP_APP "hello_cleanup"
#define TOY_BLOCK_SERVER_RESOURCE_TYPE "TOY_BLOCK"
#define TOY_FILE_SERVER_RESOURCE_TYPE "TOY_FILE"
#define TOY_DB_SERVER_RESOURCE_TYPE "TOY_DB"
#define N_REQUESTS 10

static ads_client_context_t ads_conn;
static pd_client_context_t pd_conn;
static ep_client_context_t self_ep;

// Track the type/space ID of the toy_block server
static gpi_cap_t toy_block_type;
static gpi_space_id_t toy_block_space_id;

// Track the type/space ID of the toy_file server
static gpi_cap_t toy_file_type;
static gpi_space_id_t toy_file_space_id;

// Track the type/space ID of the toy_db server
static gpi_cap_t toy_db_type;
static gpi_space_id_t toy_db_space_id;

// Setup before all tests
static int setup(env_t env)
{
    int error;

    /* Initialize the ADS */
    ads_conn.ep = sel4gpi_get_rde_by_space_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_VMR);

    /* Initialize the PD */
    pd_conn = sel4gpi_get_pd_conn();

    /* Create EP to listen for test results */
    error = sel4gpi_alloc_endpoint(&self_ep);
    test_assert(error == 0);

    return error;
}

int start_toy_cleanup_process(hello_cleanup_mode_t mode, uint32_t n_client_requests,
                              ep_client_context_t *ep, pd_client_context_t *hello_pd)
{
    int error;

    if (mode == HELLO_CLEANUP_TOY_BLOCK_SERVER_MODE)
    {
        // Start the server with the resource server utility function
        error = start_resource_server_pd_args(0, 0, HELLO_CLEANUP_APP,
                                              (seL4_Word *)&mode, 1,
                                              hello_pd, &toy_block_space_id);

        test_assert(error == 0);

        toy_block_type = sel4gpi_get_resource_type_code(TOY_BLOCK_SERVER_RESOURCE_TYPE);
        return 0;
    }
    else if (mode == HELLO_CLEANUP_TOY_FILE_SERVER_MODE)
    {
        // Start the server with the resource server utility function
        error = start_resource_server_pd_args(toy_block_type, toy_block_space_id, HELLO_CLEANUP_APP,
                                              (seL4_Word *)&mode, 1,
                                              hello_pd, &toy_file_space_id);

        test_assert(error == 0);

        toy_file_type = sel4gpi_get_resource_type_code(TOY_FILE_SERVER_RESOURCE_TYPE);
        return 0;
    }
    else if (mode == HELLO_CLEANUP_TOY_DB_SERVER_MODE)
    {
        // Start the server with the resource server utility function
        error = start_resource_server_pd_args(toy_file_type, toy_file_space_id, HELLO_CLEANUP_APP,
                                              (seL4_Word *)&mode, 1,
                                              hello_pd, &toy_db_space_id);

        test_assert(error == 0);

        toy_db_type = sel4gpi_get_resource_type_code(TOY_DB_SERVER_RESOURCE_TYPE);
        return 0;
    }

    // Otherwise, start the process normally
    sel4gpi_runnable_t runnable = {0};
    pd_config_t *cfg = sel4gpi_configure_process(HELLO_CLEANUP_APP, DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES, &runnable);
    test_assert(cfg != NULL);

    *hello_pd = runnable.pd;

    // Setup the hello PD's args
    int argc = 4;
    seL4_Word args[argc];
    // Second arg is unused, leave blank so we can use the same main function as hello server
    args[2] = mode;
    args[3] = n_client_requests;

    // Copy the parent ep
    error = pd_client_send_cap(hello_pd, ep->ep, &args[0]);
    test_assert(error == 0);

    // Share an RDE for client
    if (mode == HELLO_CLEANUP_TOY_BLOCK_CLIENT_MODE)
    {
        sel4gpi_add_rde_config(cfg, toy_block_type, toy_block_space_id);
    }
    else if (mode == HELLO_CLEANUP_TOY_FILE_CLIENT_MODE)
    {
        sel4gpi_add_rde_config(cfg, toy_file_type, toy_file_space_id);
    }
    else if (mode == HELLO_CLEANUP_TOY_DB_CLIENT_MODE)
    {
        sel4gpi_add_rde_config(cfg, toy_db_type, toy_db_space_id);
    }

    // Start it
    error = sel4gpi_prepare_pd(cfg, &runnable, argc, args);
    test_error_eq(error, 0);

    error = sel4gpi_start_pd(&runnable);
    test_error_eq(error, 0);

    // The hello processes are simple and won't message to say they're started

    sel4gpi_config_destroy(cfg);
    return 0;
}

/**
 * Simple test scenario for cleanup policies with one server/client and one resource type
 *
 * Scenario:
 * - Server PD serves toy_blocks
 * - Client PD requests toy_blocks from server
 * - Dummy PD does nothing
 *
 * Crash the server PD
 *
 * Expected outcome of PD_CLEANUP_RESOURCES_DIRECT or PD_CLEANUP_RESOURCES_RECURSIVE:
 * - Dummy PD is unaffected
 * - Client PD remains running, but no longer holds any toy_block resource
 *   - It also loses the toy_block RDE
 *
 * Expected outcome of PD_CLEANUP_DEPENDENTS_DIRECT or PD_CLEANUP_DEPENDENTS_RECURSIVE:
 * - Dummy PD is unaffected
 * - Client PD is killed
 */
int test_cleanup_policy_1(env_t env)
{
    int error;

    setup(env);

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    /* Start the PDs */
    pd_client_context_t hello_server_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_BLOCK_SERVER_MODE, N_REQUESTS, &self_ep, &hello_server_pd);
    test_assert(error == 0);

    pd_client_context_t hello_client_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_BLOCK_CLIENT_MODE, N_REQUESTS, &self_ep, &hello_client_pd);
    test_assert(error == 0);

    pd_client_context_t hello_dummy_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_NOTHING_MODE, N_REQUESTS, &self_ep, &hello_dummy_pd);
    test_assert(error == 0);

#ifdef CONFIG_DEBUG_BUILD
    // Name the PDs for model extraction
    error = pd_client_set_name(&hello_server_pd, "toy_block_server");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_client_pd, "toy_block_client");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_dummy_pd, "dummy_pd");
    test_assert(error == 0);
#endif

    /* Remove RDE from test process so that it won't be cleaned up by recursive cleanup */
    error = pd_client_remove_rde(&pd_conn, toy_block_type, toy_block_space_id);
    test_assert(error == 0);

    /* Wait for clients to finish making requests */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    for (int i = 0; i < 2; i++)
    {
        tag = sel4gpi_recv(self_ep.raw_endpoint, NULL);
        error = seL4_MessageInfo_get_label(tag);
        test_assert(error == 0);
    }

    // Print model state
    printf("Dumping model state before crash\n");
    extract_model(&pd_conn);

    // Crash hello server
    printf("Crashing toy_block server PD\n");
    error = pd_client_terminate(&hello_server_pd);
    test_assert(error == 0);

    // Print model state
    printf("Dumping model state after crash\n");
    extract_model(&pd_conn);

    /* Cleanup PDs */
    test_error_eq(maybe_terminate_pd(&hello_client_pd), 0);
    test_error_eq(maybe_terminate_pd(&hello_dummy_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPICL001, "Test the PD cleanup policy 1", test_cleanup_policy_1, true)

/**
 * More complicated test scenario for cleanup policies with two servers/clients and two resource types
 *
 * Scenario:
 * - Toy block server server PD serves toy_blocks
 * - Toy file server PD
 *     - Requests toy_blocks from toy_block server
 *     - Serves toy_file resources
 *     - Toy file map to toy_blocks
 * - Client PD 1 requests toy_blocks from toy_block server
 * - Client PD 2 requests toy_file from toy_file toy_file server
 * - Dummy PD does nothing
 *
 * Crash the toy_block server PD
 *
 * Expected outcome of PD_CLEANUP_RESOURCES_RECURSIVE:
 * - Dummy PD is unaffected
 * - Toy file server remains running, but it may no longer be operational
 *   - It loses the toy_block RDE
 * - Client PD 1 remains running, but no longer holds any toy_block resource
 *   - It also loses the toy_block RDE
 * - Client PD 2 remains running, but no longer holds any toy_file resource
 *   - It also loses the toy_file RDE
 *
 * Expected outcome of PD_CLEANUP_DEPENDENTS_DIRECT:
 * - Dummy PD is unaffected
 * - Toy file server is killed
 * - Client PD 1 is killed
 * - Client PD 2 remains running, but no longer holds any toy_file resource
 *   - It also loses the toy_file RDE
 *
 * Expected outcome of PD_CLEANUP_DEPENDENTS_RECURSIVE:
 * - Dummy PD is unaffected
 * - Toy file server is killed
 * - Client PD 1 is killed
 * - Client PD 2 is killed
 */
int test_cleanup_policy_2(env_t env)
{
    int error;

    setup(env);

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    /* Start the PDs */
    pd_client_context_t hello_server_toy_block_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_BLOCK_SERVER_MODE, N_REQUESTS,
                                      &self_ep, &hello_server_toy_block_pd);
    test_assert(error == 0);

    pd_client_context_t hello_server_toy_file_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_FILE_SERVER_MODE, N_REQUESTS,
                                      &self_ep, &hello_server_toy_file_pd);
    test_assert(error == 0);

    pd_client_context_t hello_client_toy_block_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_BLOCK_CLIENT_MODE, N_REQUESTS,
                                      &self_ep, &hello_client_toy_block_pd);
    test_assert(error == 0);

    pd_client_context_t hello_client_toy_file_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_FILE_CLIENT_MODE, N_REQUESTS,
                                      &self_ep, &hello_client_toy_file_pd);
    test_assert(error == 0);

    pd_client_context_t hello_dummy_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_NOTHING_MODE, N_REQUESTS, &self_ep, &hello_dummy_pd);
    test_assert(error == 0);

#ifdef CONFIG_DEBUG_BUILD
    // Name the PDs for model extraction
    error = pd_client_set_name(&hello_server_toy_block_pd, "toy_block_server");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_client_toy_block_pd, "toy_block_client");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_server_toy_file_pd, "toy_file_server");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_client_toy_file_pd, "toy_file_client");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_dummy_pd, "dummy_pd");
    test_assert(error == 0);
#endif

    /* Remove RDEs from test process so that it won't be cleaned up by recursive cleanup */
    error = pd_client_remove_rde(&pd_conn, toy_block_type, toy_block_space_id);
    test_assert(error == 0);

    error = pd_client_remove_rde(&pd_conn, toy_file_type, toy_file_space_id);
    test_assert(error == 0);

    /* Wait for clients to finish making requests */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    for (int i = 0; i < 3; i++)
    {
        tag = sel4gpi_recv(self_ep.raw_endpoint, NULL);
        error = seL4_MessageInfo_get_label(tag);
        test_assert(error == 0);
    }

    // Print model state
    printf("Dumping model state before crash\n");
    extract_model(&pd_conn);

    /* Crash a PD */
    printf("Crashing toy_block server PD\n");
    error = pd_client_terminate(&hello_server_toy_block_pd);
    test_assert(error == 0);

    // Print model state
    printf("Dumping model state after crash\n");
    extract_model(&pd_conn);

    /* Cleanup PDs */
    test_error_eq(maybe_terminate_pd(&hello_server_toy_file_pd), 0);
    test_error_eq(maybe_terminate_pd(&hello_client_toy_file_pd), 0);
    test_error_eq(maybe_terminate_pd(&hello_client_toy_block_pd), 0);
    test_error_eq(maybe_terminate_pd(&hello_dummy_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPICL002, "Test the PD cleanup policy 2", test_cleanup_policy_2, true)

int test_cleanup_policy_3(env_t env)
{
    int error;

    setup(env);

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    /* Start the PDs */
    pd_client_context_t hello_server_toy_block_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_BLOCK_SERVER_MODE, N_REQUESTS,
                                      &self_ep, &hello_server_toy_block_pd);
    test_assert(error == 0);

    pd_client_context_t hello_server_toy_file_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_FILE_SERVER_MODE, N_REQUESTS,
                                      &self_ep, &hello_server_toy_file_pd);
    test_assert(error == 0);

    pd_client_context_t hello_server_toy_db_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_DB_SERVER_MODE, N_REQUESTS, &self_ep, &hello_server_toy_db_pd);
    test_assert(error == 0);

    pd_client_context_t hello_client_toy_block_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_BLOCK_CLIENT_MODE, N_REQUESTS,
                                      &self_ep, &hello_client_toy_block_pd);
    test_assert(error == 0);

    pd_client_context_t hello_client_toy_file_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_FILE_CLIENT_MODE, N_REQUESTS,
                                      &self_ep, &hello_client_toy_file_pd);
    test_assert(error == 0);

    pd_client_context_t hello_client_toy_db_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_TOY_DB_CLIENT_MODE, N_REQUESTS, &self_ep, &hello_client_toy_db_pd);
    test_assert(error == 0);

    pd_client_context_t hello_dummy_pd;
    error = start_toy_cleanup_process(HELLO_CLEANUP_NOTHING_MODE, N_REQUESTS, &self_ep, &hello_dummy_pd);
    test_assert(error == 0);

#ifdef CONFIG_DEBUG_BUILD
    // Name the PDs for model extraction
    error = pd_client_set_name(&hello_server_toy_block_pd, "toy_block_server");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_client_toy_block_pd, "toy_block_client");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_server_toy_file_pd, "toy_file_server");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_client_toy_file_pd, "toy_file_client");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_server_toy_db_pd, "toy_db_server");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_client_toy_db_pd, "toy_db_client");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_dummy_pd, "dummy_pd");
    test_assert(error == 0);
#endif

    /* Remove RDEs from test process so that it won't be cleaned up by recursive cleanup */
    error = pd_client_remove_rde(&pd_conn, toy_block_type, toy_block_space_id);
    test_assert(error == 0);

    error = pd_client_remove_rde(&pd_conn, toy_file_type, toy_file_space_id);
    test_assert(error == 0);

    error = pd_client_remove_rde(&pd_conn, toy_db_type, toy_db_space_id);
    test_assert(error == 0);

    /* Wait for clients to finish making requests */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    for (int i = 0; i < 4; i++)
    {
        tag = sel4gpi_recv(self_ep.raw_endpoint, NULL);
        error = seL4_MessageInfo_get_label(tag);
        test_assert(error == 0);
    }

    // Print model state
    printf("Dumping model state before crash\n");
    extract_model(&pd_conn);

    /* Crash a PD */
    printf("Crashing toy_block server PD\n");
    error = pd_client_terminate(&hello_server_toy_block_pd);
    test_assert(error == 0);

    // Print model state
    printf("Dumping model state after crash\n");
    extract_model(&pd_conn);
    
    /* Cleanup PDs */
    test_error_eq(maybe_terminate_pd(&hello_server_toy_file_pd), 0);
    test_error_eq(maybe_terminate_pd(&hello_server_toy_db_pd), 0);
    test_error_eq(maybe_terminate_pd(&hello_client_toy_db_pd), 0);
    test_error_eq(maybe_terminate_pd(&hello_client_toy_file_pd), 0);
    test_error_eq(maybe_terminate_pd(&hello_client_toy_block_pd), 0);
    test_error_eq(maybe_terminate_pd(&hello_dummy_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPICL003, "Test the PD cleanup policy 3", test_cleanup_policy_3, true)