#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <vka/capops.h>
#include <sel4test/test.h>
#include <sel4test/macros.h>
#include "../test.h"
#include "../helpers.h"

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_utils.h>

#include <ramdisk_client.h>
#include <fs_client.h>

#define KVSTORE_SERVER_APP "kvstore_server"
#define HELLO_KVSTORE_APP "hello_kvstore"

static ads_client_context_t ads_conn;
static pd_client_context_t pd_conn;
static seL4_CPtr self_ep;

// Setup before all tests
static int setup(env_t env)
{
    int error;

    /* Initialize the ADS */
    vka_cspace_make_path(&env->vka, sel4gpi_get_rde_by_ns_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_ADS), &ads_conn.badged_server_ep_cspath);

    /* Initialize the PD */
    vka_cspace_make_path(&env->vka, sel4gpi_get_pd_cap(), &pd_conn.badged_server_ep_cspath);

    /* Start ramdisk server process */
    uint64_t ramdisk_id;
    seL4_CPtr ramdisk_pd_cap;
    error = start_ramdisk_pd(&ramdisk_pd_cap, &ramdisk_id);
    test_assert(error == 0);

    /* Start fs server process */
    uint64_t fs_id;
    seL4_CPtr fs_pd_cap;
    error = start_xv6fs_pd(ramdisk_id, ramdisk_pd_cap, &fs_pd_cap, &fs_id);
    test_assert(error == 0);

    /* Add FS ep to RDE */
    error = pd_client_add_rde(&pd_conn, fs_pd_cap, fs_id, NSID_DEFAULT);
    test_assert(error == 0);
    seL4_CPtr fs_client_ep = sel4gpi_get_rde(GPICAP_TYPE_FILE);

    /* Create EP to listen for test results */
    error = pd_client_alloc_ep(&pd_conn, &self_ep);
    test_assert(error == 0);

    return error;
}

/**
 * Starts the kvstore server process
 *
 * @param kvstore_ep returns the kvstore server's ep
 */
static int start_kvstore_server(seL4_CPtr *kvstore_ep)
{
    int error;

    // Create a new PD
    pd_client_context_t new_pd;
    seL4_CPtr free_slot;
    error = pd_client_next_slot(&pd_conn, &free_slot);
    test_assert(error == 0);
    error = pd_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_PD), free_slot, &new_pd);
    test_assert(error == 0);

    // Create a new ADS Cap, which will be in the context of a PD and image
    ads_client_context_t new_ads;
    error = pd_client_next_slot(&pd_conn, &free_slot);
    test_assert(error == 0);

    error = ads_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_ADS), free_slot, &new_ads);
    test_assert(error == 0);

    // Make a new AS, loads an image
    error = pd_client_load(&new_pd, &new_ads, KVSTORE_SERVER_APP);
    test_assert(error == 0);

    // Setup the hello PD's args
    int argc = 1;
    seL4_Word args[argc];

    // Copy the parent ep
    error = pd_client_send_cap(&new_pd, self_ep, &args[0]);
    test_assert(error == 0);

    // Give the FS RDE
    error = pd_client_share_rde(&new_pd, GPICAP_TYPE_FILE, NSID_DEFAULT);
    test_assert(error == 0);

    // Also need to give the MO rde since FS clients need some MO for requests
    error = pd_client_share_rde(&new_pd, GPICAP_TYPE_MO, NSID_DEFAULT);
    test_assert(error == 0);

    // Start it
    error = pd_client_start(&new_pd, argc, args);
    test_assert(error == 0);

    // Wait for it to finish starting
    seL4_CPtr receive_slot;
    error = pd_client_next_slot(&pd_conn, &receive_slot);
    seL4_SetCapReceivePath(PD_CAP_ROOT, receive_slot, PD_CAP_DEPTH);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep, NULL);
    int n_caps = seL4_MessageInfo_get_extraCaps(tag);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);
    test_assert(n_caps == 1);

    *kvstore_ep = receive_slot;

    return 0;
}

/**
 * Starts the hello test process that accesses kvstore
 *
 * @param use_remote_kvstore If true, hello will make requests
 *                           to remote kvstore server PD
 * @param kvstore_ep ep to use for remote kvstore (optional)
 * @param hello_pd returns the pd resource for the hello process
 */
static int start_hello_kvstore(bool use_remote_kvstore, seL4_CPtr kvstore_ep, pd_client_context_t *hello_pd)
{
    int error;

    // Create a new PD
    pd_client_context_t new_pd;
    seL4_CPtr free_slot;
    error = pd_client_next_slot(&pd_conn, &free_slot);
    test_assert(error == 0);
    error = pd_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_PD), free_slot, &new_pd);
    test_assert(error == 0);
    *hello_pd = new_pd;

    // Create a new ADS Cap, which will be in the context of a PD and image
    ads_client_context_t new_ads;
    error = pd_client_next_slot(&pd_conn, &free_slot);
    test_assert(error == 0);

    error = ads_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_ADS), free_slot, &new_ads);
    test_assert(error == 0);

    // Make a new AS, loads an image
    error = pd_client_load(&new_pd, &new_ads, HELLO_KVSTORE_APP);
    test_assert(error == 0);

    // Setup the hello PD's args
    int argc = 2;
    seL4_Word args[argc];

    // Copy the parent ep
    error = pd_client_send_cap(&new_pd, self_ep, &args[0]);
    test_assert(error == 0);

    // Copy the kvstore ep, if applicable
    if (use_remote_kvstore)
    {
        error = pd_client_send_cap(&new_pd, kvstore_ep, &args[1]);
        test_assert(error == 0);
    }
    else
    {
        args[1] = 0;
    }

    // Give the FS RDE, if not using remote kvstore
    if (!use_remote_kvstore)
    {
        error = pd_client_share_rde(&new_pd, GPICAP_TYPE_FILE, NSID_DEFAULT);
        test_assert(error == 0);

        // Also need to give the MO rde since FS clients need some MO for requests
        error = pd_client_share_rde(&new_pd, GPICAP_TYPE_MO, NSID_DEFAULT);
        test_assert(error == 0);
    }

    // Start it
    error = pd_client_start(&new_pd, argc, args);
    test_assert(error == 0);

    return 0;
}

int test_kvstore_lib_in_same_pd(env_t env)
{
    int error;

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    error = setup(env);
    test_assert(error == 0);

    /* Start the combined app/lib PD */
    pd_client_context_t hello_pd;
    error = start_hello_kvstore(false, 0, &hello_pd);

    /* Wait for test result */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    /* Print hello model state */
    error = pd_client_dump(&hello_pd, NULL, 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPIKV001, "Test kvstore with app and lib in the same PD, same ADS", test_kvstore_lib_in_same_pd, true)

int test_kvstore_lib_in_diff_pd(env_t env)
{
    int error;

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    error = setup(env);
    test_assert(error == 0);

    /* Start the kvstore PD */
    seL4_CPtr kvstore_ep;
    error = start_kvstore_server(&kvstore_ep);

    /* Start the app PD */
    pd_client_context_t hello_pd;
    error = start_hello_kvstore(true, kvstore_ep, &hello_pd);

    /* Wait for test result */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    /* Print hello model state */
    error = pd_client_dump(&hello_pd, NULL, 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPIKV002, "Test kvstore with app and lib in different PDs", test_kvstore_lib_in_same_pd, true)
