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
#include <sel4gpi/pd_creation.h>

#include <ramdisk_client.h>
#include <fs_client.h>

#define KVSTORE_SERVER_APP "kvstore_server"
#define HELLO_KVSTORE_APP "hello_kvstore"
#define DUMP_MODEL false

static ads_client_context_t ads_conn;
static pd_client_context_t pd_conn;
static ep_client_context_t self_ep;

static uint64_t ramdisk_id;
static pd_client_context_t ramdisk_pd;
static uint64_t fs_id;
static pd_client_context_t fs_pd;
static uint64_t fs_2_id;
static pd_client_context_t fs_2_pd;
static gpi_cap_t file_cap_type;

typedef enum _kvstore_mode
{
    SAME_THREAD,
    SEPARATE_ADS,
    SEPARATE_THREAD,
    SEPARATE_PROC
} kvstore_mode_t;

static void dump_model()
{
#if DUMP_MODEL
    /* Print model state */
    pd_client_dump(&pd_conn, NULL, 0);
#endif
}

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

    /* Start ramdisk server process */
    error = start_ramdisk_pd(&ramdisk_pd.badged_server_ep_cspath.capPtr, &ramdisk_id);
    test_assert(error == 0);

    /* Start FS server process */
    error = start_xv6fs_pd(ramdisk_id, &fs_pd.badged_server_ep_cspath.capPtr, &fs_id);
    test_assert(error == 0);

    /* Add FS ep to RDE */
    file_cap_type = sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME);
    seL4_CPtr fs_client_ep = sel4gpi_get_rde(file_cap_type);

    /* Create EP to listen for test results */
    error = sel4gpi_alloc_endpoint(&self_ep);
    test_assert(error == 0);

    return error;
}

/**
 * Starts the kvstore server process
 *
 * @param kvstore_ep returns the kvstore server's ep
 * @param fs_nsid namespace ID of fs to share
 * @param kvstore_pd  returns the pd resource for the kvstore process
 */
static int start_kvstore_server(seL4_CPtr *kvstore_ep, uint64_t fs_nsid, pd_client_context_t *kvstore_pd)
{
    int error;

    sel4gpi_runnable_t runnable = {0};
    pd_config_t *cfg = sel4gpi_configure_process(KVSTORE_SERVER_APP, DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES, &runnable);
    test_assert(cfg != NULL);

    *kvstore_pd = runnable.pd;

    // Setup the hello PD's args
    int argc = 1;
    seL4_Word args[argc];

    // Copy the parent ep
    error = pd_client_send_cap(kvstore_pd, self_ep.badged_server_ep_cspath.capPtr, &args[0]);
    test_assert(error == 0);

    // Share an FS and EP RDE
    sel4gpi_add_rde_config(cfg, file_cap_type, fs_nsid);
    sel4gpi_add_rde_config(cfg, GPICAP_TYPE_EP, RESSPC_ID_NULL);

    // Start it
    error = sel4gpi_prepare_pd(cfg, &runnable, argc, args);
    test_error_eq(error, 0);

    error = sel4gpi_start_pd(&runnable);
    test_error_eq(error, 0);

    // Wait for it to finish starting
    seL4_CPtr receive_slot;
    error = pd_client_next_slot(&pd_conn, &receive_slot);
    test_error_eq(error, 0);
    test_assert(receive_slot != seL4_CapNull);

    seL4_SetCapReceivePath(PD_CAP_ROOT, receive_slot, PD_CAP_DEPTH);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    int n_caps = seL4_MessageInfo_get_extraCaps(tag);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);
    test_assert(n_caps == 1);

    *kvstore_ep = receive_slot;

    sel4gpi_config_destroy(cfg);
    return 0;
}

/**
 * Starts the hello test process that accesses kvstore
 *
 * @param kvstore_mode the operating mode of the hello_kvstore app
 * @param kvstore_ep ep to use for remote kvstore (optional)
 * @param hello_pd returns the pd resource for the hello process
 * @param fs_nsid namespace ID of fs to share
 */
static int start_hello_kvstore(kvstore_mode_t kvstore_mode,
                               seL4_CPtr kvstore_ep,
                               pd_client_context_t *hello_pd,
                               uint64_t fs_nsid)
{
    int error;

    // Setup the hello PD's args
    int argc = 3;
    seL4_Word args[argc];

    sel4gpi_runnable_t runnable = {0};
    pd_config_t *cfg = sel4gpi_configure_process(HELLO_KVSTORE_APP, DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES, &runnable);
    test_assert(cfg != NULL);

    *hello_pd = runnable.pd;

    // Copy the parent ep
    error = pd_client_send_cap(hello_pd, self_ep.badged_server_ep_cspath.capPtr, &args[0]);
    test_assert(error == 0);

    args[2] = kvstore_mode;

    // Copy the kvstore ep, if applicable
    if (kvstore_mode == SEPARATE_PROC)
    {
        error = pd_client_send_cap(hello_pd, kvstore_ep, &args[1]);
        test_assert(error == 0);
    }
    else
    {
        args[1] = 0;
    }

    // Share an FS RDE
    sel4gpi_add_rde_config(cfg, file_cap_type, fs_nsid);
    test_assert(error == 0);

    // Share necessary RDEs to start threads
    if (kvstore_mode == SEPARATE_THREAD)
    {
        sel4gpi_add_rde_config(cfg, GPICAP_TYPE_EP, RESSPC_ID_NULL);
        sel4gpi_add_rde_config(cfg, GPICAP_TYPE_PD, RESSPC_ID_NULL);
        sel4gpi_add_rde_config(cfg, GPICAP_TYPE_CPU, RESSPC_ID_NULL);
    }

    // share the ADS RDE if we're to make new ADSes
    if (kvstore_mode == SEPARATE_ADS)
    {
        sel4gpi_add_rde_config(cfg, GPICAP_TYPE_ADS, RESSPC_ID_NULL);
    }

    // Start it
    error = sel4gpi_prepare_pd(cfg, &runnable, argc, args);
    test_error_eq(error, 0);

    error = sel4gpi_start_pd(&runnable);
    test_error_eq(error, 0);

    sel4gpi_config_destroy(cfg);
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
    error = start_hello_kvstore(SAME_THREAD, 0, &hello_pd, RESSPC_ID_NULL);

    /* Wait for test result */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    dump_model();

    /* Cleanup servers */
    error = pd_client_disconnect(&hello_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&fs_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&ramdisk_pd);
    test_assert(error == 0);

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
    pd_client_context_t kvstore_pd;
    seL4_CPtr kvstore_ep;
    error = start_kvstore_server(&kvstore_ep, RESSPC_ID_NULL, &kvstore_pd);
    test_assert(error == 0);

    /* Start the app PD */
    pd_client_context_t hello_pd;
    error = start_hello_kvstore(SEPARATE_PROC, kvstore_ep, &hello_pd, RESSPC_ID_NULL);

    /* Wait for test result */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    dump_model();

    /* Cleanup servers */
    error = pd_client_disconnect(&hello_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&kvstore_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&fs_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&ramdisk_pd);
    test_assert(error == 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPIKV002, "Test kvstore with app and lib in different PDs, same FS, same NS", test_kvstore_lib_in_diff_pd, true)

int test_kvstore_diff_namespace(env_t env)
{
    int error;

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    error = setup(env);
    test_assert(error == 0);

    /* Create the FS namespaces */
    seL4_CPtr fs_ep = sel4gpi_get_rde(file_cap_type);
    uint64_t nsid_1, nsid_2;

    error = resource_server_client_new_ns(fs_ep, &nsid_1);
    test_assert(error == 0);

    error = resource_server_client_new_ns(fs_ep, &nsid_2);
    test_assert(error == 0);

    /* Start the kvstore PD */
    seL4_CPtr kvstore_ep;
    pd_client_context_t kvstore_pd;
    error = start_kvstore_server(&kvstore_ep, nsid_1, &kvstore_pd);
    test_assert(error == 0);

    /* Start the app PD */
    pd_client_context_t hello_pd;
    error = start_hello_kvstore(SEPARATE_PROC, kvstore_ep, &hello_pd, nsid_2);
    test_assert(error == 0);

    /* Wait for test result */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    dump_model();

    /* Cleanup PDs */
    error = pd_client_disconnect(&hello_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&kvstore_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&fs_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&ramdisk_pd);
    test_assert(error == 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPIKV003, "Test app and lib with same FS, different namespace", test_kvstore_diff_namespace, true)

int test_kvstore_diff_fs(env_t env)
{
    int error;

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    error = setup(env);
    test_assert(error == 0);

    /* Start second fs server process */
    error = start_xv6fs_pd(ramdisk_id, &fs_2_pd.badged_server_ep_cspath.capPtr, &fs_2_id);
    test_assert(error == 0);

    /* Start the kvstore PD */
    seL4_CPtr kvstore_ep;
    pd_client_context_t kvstore_pd;
    error = start_kvstore_server(&kvstore_ep, RESSPC_ID_NULL, &kvstore_pd);
    test_assert(error == 0);

    /* Start the app PD */
    pd_client_context_t hello_pd;
    error = start_hello_kvstore(SEPARATE_PROC, kvstore_ep, &hello_pd, fs_2_id);
    test_assert(error == 0);

    /* Wait for test result */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    dump_model();

    /* Cleanup PDs */
    error = pd_client_disconnect(&hello_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&kvstore_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&fs_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&fs_2_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&ramdisk_pd);
    test_assert(error == 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPIKV004, "Test app and lib with different", test_kvstore_diff_fs, true)

int test_kvstore_lib_same_pd_diff_ads(env_t env)
{
    int error;

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    error = setup(env);
    test_assert(error == 0);

    // /* Start the combined app/lib PD */
    pd_client_context_t hello_pd;
    error = start_hello_kvstore(SEPARATE_ADS, 0, &hello_pd, RESSPC_ID_NULL);

    /* Wait for test result */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    dump_model();

    /* Cleanup PDs */
    error = pd_client_disconnect(&hello_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&fs_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&ramdisk_pd);
    test_assert(error == 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPIKV005, "Test kvstore with app and lib in the same PD, different ADS", test_kvstore_lib_same_pd_diff_ads, true)

int test_kvstore_diff_threads(env_t env)
{
    int error;

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    error = setup(env);
    test_assert(error == 0);

    /* Start the combined app/lib PD */
    pd_client_context_t hello_pd;
    error = start_hello_kvstore(SEPARATE_THREAD, 0, &hello_pd, RESSPC_ID_NULL);

    /**
     * We don't actually destroy the second thread because we don't start it as a PD here
     * Then not all of its resources are destroyed
     * Once fixed, we can properly destroy both PDs
     */

    /* Wait for test result */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    dump_model();

    /* Cleanup PDs */
    error = pd_client_disconnect(&hello_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&fs_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&ramdisk_pd);
    test_assert(error == 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPIKV006, "Test kvstore with app and lib in the same PD, different threads", test_kvstore_diff_threads, true)

int test_kvstore_two_sets(env_t env)
{
    int error;

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    error = setup(env);
    test_assert(error == 0);

    /* Start the kvstore PD 1 */
    seL4_CPtr kvstore_ep_1;
    pd_client_context_t kvstore_pd_1;
    error = start_kvstore_server(&kvstore_ep_1, RESSPC_ID_NULL, &kvstore_pd_1);
    test_assert(error == 0);

    /* Start the kvstore PD 2 */
    seL4_CPtr kvstore_ep_2;
    pd_client_context_t kvstore_pd_2;
    error = start_kvstore_server(&kvstore_ep_2, RESSPC_ID_NULL, &kvstore_pd_2);
    test_assert(error == 0);

    /* Start the app PD 1 */
    pd_client_context_t hello_pd_1;
    error = start_hello_kvstore(SEPARATE_PROC, kvstore_ep_1, &hello_pd_1, RESSPC_ID_NULL);

    /* Start the app PD 2 */
    pd_client_context_t hello_pd_2;
    error = start_hello_kvstore(SEPARATE_PROC, kvstore_ep_2, &hello_pd_2, RESSPC_ID_NULL);

    /* Wait for test result 1 */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    /* Wait for test result 2 */
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    dump_model();

    /* Cleanup PDs */
    error = pd_client_disconnect(&hello_pd_1);
    test_assert(error == 0);
    error = pd_client_disconnect(&hello_pd_2);
    test_assert(error == 0);
    error = pd_client_disconnect(&kvstore_pd_1);
    test_assert(error == 0);
    error = pd_client_disconnect(&kvstore_pd_2);
    test_assert(error == 0);
    error = pd_client_disconnect(&fs_pd);
    test_assert(error == 0);
    error = pd_client_disconnect(&ramdisk_pd);
    test_assert(error == 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST(GPIKV007, "Test kvstore with app and lib in different PDs, 2 sets of each", test_kvstore_two_sets, true)