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
#define POKEMART_RESOURCE_TYPE "POKEBALL"
#define DAYCARE_RESOURCE_TYPE "POKEMON"

static ads_client_context_t ads_conn;
static pd_client_context_t pd_conn;
static ep_client_context_t self_ep;

// Track the type/space ID of the pokemart server
static gpi_cap_t pokemart_type;
static uint64_t pokemart_space_id;

// Track the type/space ID of the daycare server
static gpi_cap_t daycare_type;
static uint64_t daycare_space_id;

// This needs to be the same as the definition in hello-cleanup/main.c
typedef enum _hello_mode
{
    HELLO_CLEANUP_SERVER_POKEMART_MODE, ///< Process will serve pokeballs
    HELLO_CLEANUP_SERVER_DAYCARE_MODE,  ///< Process will serve pokemon
    HELLO_CLEANUP_CLIENT_POKEMART_MODE, ///< Process will request pokeballs
    HELLO_CLEANUP_CLIENT_DAYCARE_MODE,  ///< Process will request pokemon
    HELLO_CLEANUP_NOTHING_MODE,         ///< Process will do nothing
} hello_mode_t;

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

/**
 * Starts the hello-cleanup process
 *
 * @param mode the mode for hello to run in
 * @param hello_pd  returns the pd resource for the hello process
 * @return 0 on success, error otherwise
 */
static int start_hello(hello_mode_t mode, pd_client_context_t *hello_pd)
{
    int error;

    if (mode == HELLO_CLEANUP_SERVER_POKEMART_MODE)
    {
        // Start the server with the resource server utility function
        error = start_resource_server_pd_args(0, 0, HELLO_CLEANUP_APP, &mode, 1,
                                              &hello_pd->ep, &pokemart_space_id);

        test_assert(error == 0);

        pokemart_type = sel4gpi_get_resource_type_code(POKEMART_RESOURCE_TYPE);
        return 0;
    }
    else if (mode == HELLO_CLEANUP_SERVER_DAYCARE_MODE)
    {
        // Start the server with the resource server utility function
        error = start_resource_server_pd_args(pokemart_type, pokemart_space_id, HELLO_CLEANUP_APP, &mode, 1,
                                              &hello_pd->ep, &daycare_space_id);

        test_assert(error == 0);

        daycare_type = sel4gpi_get_resource_type_code(DAYCARE_RESOURCE_TYPE);
        return 0;
    }

    // Otherwise, start the process normally
    sel4gpi_runnable_t runnable = {0};
    pd_config_t *cfg = sel4gpi_configure_process(HELLO_CLEANUP_APP, DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES, &runnable);
    test_assert(cfg != NULL);

    *hello_pd = runnable.pd;

    // Setup the hello PD's args
    int argc = 3;
    seL4_Word args[argc];
    // Second arg is unused, leave blank so we can use the same main function as hello server
    args[2] = mode;

    // Copy the parent ep
    error = pd_client_send_cap(hello_pd, self_ep.ep, &args[0]);
    test_assert(error == 0);

    // Share an RDE for client
    if (mode == HELLO_CLEANUP_CLIENT_POKEMART_MODE)
    {
        sel4gpi_add_rde_config(cfg, pokemart_type, pokemart_space_id);
    }
    else if (mode == HELLO_CLEANUP_CLIENT_DAYCARE_MODE)
    {
        sel4gpi_add_rde_config(cfg, daycare_type, daycare_space_id);
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
 * - Server PD serves pokeballs
 * - Client PD requests pokeballs from server
 * - Dummy PD does nothing
 *
 * Crash the server PD
 *
 * Expected outcome of PD_CLEANUP_RESOURCES_DIRECT or PD_CLEANUP_RESOURCES_RECURSIVE:
 * - Dummy PD is unaffected
 * - Client PD remains running, but no longer holds any pokeball resource
 *   - It also loses the pokeball RDE
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
    error = start_hello(HELLO_CLEANUP_SERVER_POKEMART_MODE, &hello_server_pd);
    test_assert(error == 0);

    pd_client_context_t hello_client_pd;
    error = start_hello(HELLO_CLEANUP_CLIENT_POKEMART_MODE, &hello_client_pd);
    test_assert(error == 0);

    pd_client_context_t hello_dummy_pd;
    error = start_hello(HELLO_CLEANUP_NOTHING_MODE, &hello_dummy_pd);
    test_assert(error == 0);

#ifdef CONFIG_DEBUG_BUILD
    // Name the PDs for model extraction
    error = pd_client_set_name(&hello_server_pd, "pokemart_server");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_client_pd, "pokemart_client");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_dummy_pd, "dummy_pd");
    test_assert(error == 0);
#endif

    /* Remove RDE from test process so that it won't be cleaned up by recursive cleanup */
    error = pd_client_remove_rde(&pd_conn, pokemart_type, pokemart_space_id);
    test_assert(error == 0);

    /* Print model state before crash */
    printf("Dumping model state before crash\n");
    error = pd_client_dump(&pd_conn, NULL, 0);
    test_assert(error == 0);

    /* Crash a PD */
    printf("Crashing server PD\n");
    error = pd_client_terminate(&hello_server_pd);
    test_assert(error == 0);

    /* Print model state after crash */
    printf("Dumping model state after crash\n");
    error = pd_client_dump(&pd_conn, NULL, 0);
    test_assert(error == 0);

    /* Cleanup PDs */
    error = pd_client_terminate(&hello_client_pd);

    if (error != seL4_NoError)
    {
        printf("WARNING: Failed to cleanup hello-client PD (%d), "
               "this may be expected if the cleanup policy already destroyed it. \n",
               hello_client_pd.id);
    }

    error = pd_client_terminate(&hello_dummy_pd);
    test_assert(error == 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPICL001, "Test the PD cleanup policy 1", test_cleanup_policy_1, true)

/**
 * More complicated test scenario for cleanup policies with two servers/clients and two resource types
 *
 * Scenario:
 * - Pokemart server PD serves pokeballs
 * - Pokemon daycare server PD
 *     - Requests pokeballs from pokemart server
 *     - Serves pokemon resources
 *     - Pokemon map to pokeballs
 * - Client PD 1 requests pokeballs from pokemart server
 * - Client PD 2 requests pokemon from pokemon daycare server
 * - Dummy PD does nothing
 *
 * Crash the pokemart server PD
 *
 * Expected outcome of PD_CLEANUP_RESOURCES_RECURSIVE:
 * - Dummy PD is unaffected
 * - Pokemon daycare server remains running, but it may no longer be operational
 *   - It loses the pokeball RDE
 * - Client PD 1 remains running, but no longer holds any pokeball resource
 *   - It also loses the pokeball RDE
 * - Client PD 2 remains running, but no longer holds any pokemon resource
 *   - It also loses the pokemon RDE
 *
 * Expected outcome of PD_CLEANUP_DEPENDENTS_DIRECT:
 * - Dummy PD is unaffected
 * - Pokemon daycare server is killed
 * - Client PD 1 is killed
 * - Client PD 2 remains running, but no longer holds any pokemon resource
 *   - It also loses the pokemon RDE
 *
 * Expected outcome of PD_CLEANUP_DEPENDENTS_RECURSIVE:
 * - Dummy PD is unaffected
 * - Pokemon daycare server is killed
 * - Client PD 1 is killed
 * - Client PD 2 is killed
 */
int test_cleanup_policy_2(env_t env)
{
    int error;

    setup(env);

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    /* Start the PDs */
    pd_client_context_t hello_server_pokemart_pd;
    error = start_hello(HELLO_CLEANUP_SERVER_POKEMART_MODE, &hello_server_pokemart_pd);
    test_assert(error == 0);

    pd_client_context_t hello_server_daycare_pd;
    error = start_hello(HELLO_CLEANUP_SERVER_DAYCARE_MODE, &hello_server_daycare_pd);
    test_assert(error == 0);

    pd_client_context_t hello_client_pokemart_pd;
    error = start_hello(HELLO_CLEANUP_CLIENT_POKEMART_MODE, &hello_client_pokemart_pd);
    test_assert(error == 0);

    pd_client_context_t hello_client_daycare_pd;
    error = start_hello(HELLO_CLEANUP_CLIENT_DAYCARE_MODE, &hello_client_daycare_pd);
    test_assert(error == 0);

    pd_client_context_t hello_dummy_pd;
    error = start_hello(HELLO_CLEANUP_NOTHING_MODE, &hello_dummy_pd);
    test_assert(error == 0);

#ifdef CONFIG_DEBUG_BUILD
    // Name the PDs for model extraction
    error = pd_client_set_name(&hello_server_pokemart_pd, "pokemart_server");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_client_pokemart_pd, "pokemart_client");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_server_daycare_pd, "daycare_server");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_client_daycare_pd, "daycare_client");
    test_assert(error == 0);
    error = pd_client_set_name(&hello_dummy_pd, "dummy_pd");
    test_assert(error == 0);
#endif

    /* Remove RDEs from test process so that it won't be cleaned up by recursive cleanup */
    error = pd_client_remove_rde(&pd_conn, pokemart_type, pokemart_space_id);
    test_assert(error == 0);

    error = pd_client_remove_rde(&pd_conn, daycare_type, daycare_space_id);
    test_assert(error == 0);

    /* Print model state before crash */
    printf("Dumping model state before crash\n");
    error = pd_client_dump(&pd_conn, NULL, 0);
    test_assert(error == 0);

    /* Crash a PD */
    printf("Crashing pokemart server PD\n");
    error = pd_client_terminate(&hello_server_pokemart_pd);
    test_assert(error == 0);

    /* Print model state after crash */
    printf("Dumping model state after crash\n");
    error = pd_client_dump(&pd_conn, NULL, 0);
    test_assert(error == 0);

    /* Cleanup PDs */
    error = pd_client_terminate(&hello_server_daycare_pd);

    if (error != seL4_NoError)
    {
        printf("WARNING: Failed to cleanup hello-server-daycare PD (%d), "
               "this may be expected if the cleanup policy already destroyed it. \n",
               hello_server_daycare_pd.id);
    }

    error = pd_client_terminate(&hello_client_pokemart_pd);

    if (error != seL4_NoError)
    {
        printf("WARNING: Failed to cleanup hello-client-pokemart PD (%d), "
               "this may be expected if the cleanup policy already destroyed it. \n",
               hello_client_pokemart_pd.id);
    }

    error = pd_client_terminate(&hello_client_daycare_pd);

    if (error != seL4_NoError)
    {
        printf("WARNING: Failed to cleanup hello-client-daycare PD (%d), "
               "this may be expected if the cleanup policy already destroyed it. \n",
               hello_client_daycare_pd.id);
    }

    error = pd_client_terminate(&hello_dummy_pd);
    test_assert(error == 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPICL002, "Test the PD cleanup policy 2", test_cleanup_policy_2, true)