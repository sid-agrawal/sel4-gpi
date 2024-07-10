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
 * @file
 * Test synchronization
 * This just spawns two PDs to be synchronized via mutex
 */

#define HELLO_SYNC_APP "hello_sync"

static ads_client_context_t ads_conn;
static pd_client_context_t pd_conn;
static ep_client_context_t self_ep;

// This needs to be the same as the definition in hello-sync/main.c
typedef enum _hello_mode
{
    HELLO_SYNC_1, ///< Basic sync process 1
    HELLO_SYNC_2, ///< Basic sync process 2
} hello_mode_t;

// Setup before all tests
static int setup(env_t env)
{
    int error;

    /* Initialize the ADS */
    ads_conn = sel4gpi_get_bound_vmr_rde();

    /* Initialize the PD */
    pd_conn = sel4gpi_get_pd_conn();

    /* Create EP to listen for test results */
    error = sel4gpi_alloc_endpoint(&self_ep);
    test_assert(error == 0);

    return error;
}

typedef struct _hello_context
{
    sel4gpi_runnable_t runnable;
    pd_config_t *cfg;
    seL4_Word args[4];
} hello_context_t;

/**
 * Initialize a hello-sync process, but do not start it
 *
 * @param mode the mode for hello to run in
 * @param notif a notification capability to send for synchronization
 * @param mo some shared frame to synchronize
 * @param context fills out the hello context to be started later
 * @return 0 on success, error otherwise
 */
static int initialize_hello(hello_mode_t mode, seL4_CPtr notif, mo_client_context_t *mo, hello_context_t *context)
{
    int error;

    // Configure the process
    context->cfg = sel4gpi_configure_process(
        HELLO_SYNC_APP,
        DEFAULT_STACK_PAGES,
        DEFAULT_HEAP_PAGES,
        &context->runnable);
    test_assert(context->cfg != NULL);

    pd_client_context_t *hello_pd = &context->runnable.pd;

    // Send the notification object
    seL4_CPtr notif_slot;
    error = pd_client_send_cap(hello_pd, notif, &notif_slot);
    test_assert(error == 0);

    // Send the parent ep
    seL4_CPtr parent_ep_slot;
    error = pd_client_send_cap(hello_pd, self_ep.ep, &parent_ep_slot);
    test_assert(error == 0);

    // Attach the MO
    void *mo_vaddr;
    ads_client_context_t vmr_rde = {
        .ep = sel4gpi_get_rde_by_space_id(context->runnable.ads.id, GPICAP_TYPE_VMR)};
    error = ads_client_attach(&vmr_rde, NULL, mo, SEL4UTILS_RES_TYPE_SHARED_FRAMES, &mo_vaddr);
    test_assert(error == 0);

    // Setup the hello PD's args
    context->args[0] = (seL4_Word)mode;
    context->args[1] = (seL4_Word)notif_slot;
    context->args[2] = (seL4_Word)parent_ep_slot;
    context->args[3] = (seL4_Word)mo_vaddr;

    // Prepare it
    error = sel4gpi_prepare_pd(context->cfg, &context->runnable, 4, context->args);
    test_error_eq(error, 0);

    // Destroy the config
    sel4gpi_config_destroy(context->cfg);

    return 0;
}

/**
 * Start a hello process from a previously-initialized context
 */
static int start_hello(hello_context_t *context)
{
    int error = 0;

    // Start it
    error = sel4gpi_start_pd(&context->runnable);

    return error;
}

/**
 * Start two PDs with a shared mutex, inspect the logs to ensure that they do not enter
 * their critical section at the same time
 */
int test_mutex(env_t env)
{
    int error;

    setup(env);

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    /* Create shared MO */
    mo_client_context_t mo_conn;
    error = mo_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_MO), 1, MO_PAGE_BITS, &mo_conn);
    test_assert(error == 0);

    /* Create shared notification */
    vka_object_t notif;
    error = vka_alloc_notification(&env->vka, &notif);
    test_assert(error == 0);

    // Badge it, so we can set a value
    cspacepath_t notif_src, notif_badged;
    vka_cspace_make_path(&env->vka, notif.cptr, &notif_src);
    error = vka_cspace_alloc_path(&env->vka, &notif_badged);
    test_assert(error == 0);
    error = vka_cnode_mint(&notif_badged, &notif_src, seL4_AllRights, 0x1);
    test_assert(error == 0);

    // Set the mutex to initially available
    seL4_Signal(notif_badged.capPtr);

    /* Start the PDs */
    hello_context_t sync_pd_1;
    error = initialize_hello(HELLO_SYNC_1, notif_badged.capPtr, &mo_conn, &sync_pd_1);
    test_assert(error == 0);

    hello_context_t sync_pd_2;
    error = initialize_hello(HELLO_SYNC_2, notif_badged.capPtr, &mo_conn, &sync_pd_2);
    test_assert(error == 0);

    error = start_hello(&sync_pd_1);
    test_assert(error == 0);

    error = start_hello(&sync_pd_2);
    test_assert(error == 0);

    /* Wait for results */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);

    // First to finish
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    int error1 = seL4_MessageInfo_get_label(tag);

    // Second to finish
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    int error2 = seL4_MessageInfo_get_label(tag);

    // Cleanup both, ignore errors
    pd_client_terminate(&sync_pd_1.runnable.pd);
    pd_client_terminate(&sync_pd_2.runnable.pd);

    test_assert(error1 == 0);
    test_assert(error2 == 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}

// (XXX) Arya: This test doesn't work because it uses vka
DEFINE_TEST_OSM(GPISYNC001, "Test a simple mutex", test_mutex, false)