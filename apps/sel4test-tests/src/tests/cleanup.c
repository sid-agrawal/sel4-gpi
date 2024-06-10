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

#define HELLO_CLEANUP_APP "hello_cleanup"
#define HELLO_RESOURCE_TYPE "POKEBALL"

/**
 * Test the PD cleanup policies
 *
 * This cannot be tested programmatically
 * This test just creates a test scenario and outputs the model state after a PD has crashed
 * User will have to ensure the output model state looks correct
 *
 * The cleanup policy is currently set by the defined GPI_CLEANUP_POLICY
 */

static ads_client_context_t ads_conn;
static pd_client_context_t pd_conn;
static seL4_CPtr self_ep;
static gpi_cap_t hello_resource_type;
static uint64_t hello_space_id;

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
    vka_cspace_make_path(&env->vka,
                         sel4gpi_get_rde_by_space_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_VMR),
                         &ads_conn.badged_server_ep_cspath);

    /* Initialize the PD */
    pd_conn = sel4gpi_get_pd_conn();

    /* Create EP to listen for test results */
    error = pd_client_alloc_ep(&pd_conn, &self_ep);
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
        error = start_resource_server_pd(0, 0, HELLO_CLEANUP_APP,
                                         &hello_pd->badged_server_ep_cspath.capPtr, &hello_space_id);

        test_assert(error == 0);

        hello_resource_type = sel4gpi_get_resource_type_code(HELLO_RESOURCE_TYPE);
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
    error = pd_client_send_cap(hello_pd, self_ep, &args[0]);
    test_assert(error == 0);

    // Share an RDE for client
    if (mode == HELLO_CLEANUP_CLIENT_POKEMART_MODE)
    {
        error = pd_client_share_rde(hello_pd, hello_resource_type, hello_space_id);
        test_assert(error == 0);
    }

    // Start it
    error = sel4gpi_start_pd(cfg, &runnable, argc, args);
    test_error_eq(error, 0);

    // The hello processes are simple and won't message to say they're started

    sel4gpi_config_destroy(cfg);
    return 0;
}

/**
 * Cleanup policy 1a:
 * If a PD is killed that manages a resource space RS containing a set of resources R
 * - Remove the resources R from any PDs that hold them
 * - Remove any request edges for RS
 *
 * Scenario:
 * - Server PD serves pokeballs
 * - Client PD requests pokeballs from server
 * - Dummy PD does nothing
 *
 * Crash the server PD
 *
 * Expected outcome:
 * - Dummy PD is unaffected
 * - Client PD remains running, but no longer holds any pokeball resource
 *   - It also loses the pokeball RDE
 */
int test_cleanup_policy_1a(env_t env)
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

    /* Print model state before crash */
    printf("Dumping model state before crash\n");
    //error = pd_client_dump(&pd_conn, NULL, 0);
    test_assert(error == 0);

    /* Crash a PD */
    printf("Crashing server PD\n");
    error = pd_client_disconnect(&hello_server_pd);
    test_assert(error == 0);

    /* Print model state after crash */
    printf("Dumping model state after crash\n");
    //error = pd_client_dump(&pd_conn, NULL, 0);
    test_assert(error == 0);

    /* Cleanup PDs */
    error = pd_client_disconnect(&hello_client_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&hello_dummy_pd);
    test_assert(error == 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPICL001, "Test the PD cleanup policy 1a", test_cleanup_policy_1a, true)

/**
 * Cleanup policy 1b:
 * If a PD is killed that manages a resource space RS containing a set of resources R
 * - Remove the resources R from any PDs that hold them
 * - Remove any request edges for RS
 * - Recursively, remove any resource spaces that map to RS
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
 * Expected outcome:
 * - Dummy PD is unaffected
 * - Client PD 1 remains running, but no longer holds any pokeball resource
 *   - It also loses the pokeball RDE
 * - Client PD 2 remains running, but no longer holds any pokemon resource
 *   - It also loses the pokemon RDE
 * - Pokemon daycare server remains running, but it may no longer be operational
 *   - It loses the pokeball RDE
 */