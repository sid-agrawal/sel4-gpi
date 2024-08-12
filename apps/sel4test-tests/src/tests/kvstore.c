#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <sel4test/test.h>
#include <sel4test/macros.h>
#include "../test.h"
#include "../helpers.h"
#include "test_shared.h"

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/vmr_clientapi.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/pd_creation.h>

#include <ramdisk_client.h>
#include <fs_client.h>
#include <kvstore_shared.h>

#define KVSTORE_SERVER_APP "kvstore_server"
#define HELLO_KVSTORE_APP "hello_kvstore"

static ads_client_context_t ads_conn;
static pd_client_context_t pd_conn;
static ep_client_context_t self_ep;

static gpi_space_id_t ramdisk_id;
static pd_client_context_t ramdisk_pd;
static gpi_cap_t ramdisk_cap_type;
static gpi_space_id_t fs_id;
static pd_client_context_t fs_pd;
static gpi_space_id_t fs_2_id;
static pd_client_context_t fs_2_pd;
static gpi_cap_t file_cap_type;
static gpi_cap_t kvstore_cap_type;

// Setup before all tests
static int setup(env_t env)
{
    int error;

    /* Initialize the ADS */
    ads_conn.ep = sel4gpi_get_rde_by_space_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_VMR);

    /* Initialize the PD */
    pd_conn = sel4gpi_get_pd_conn();

    /* Start ramdisk server process */
    error = start_ramdisk_pd(&ramdisk_pd, &ramdisk_id);
    ramdisk_cap_type = sel4gpi_get_resource_type_code(BLOCK_RESOURCE_TYPE_NAME);
    test_assert(error == 0);

    /* Start FS server process */
    error = start_xv6fs_pd(ramdisk_id, &fs_pd, &fs_id);
    file_cap_type = sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME);
    test_assert(error == 0);

    error = xv6fs_client_init();
    test_assert(error == 0);

    /* Create EP to listen for test results */
    error = sel4gpi_alloc_endpoint(&self_ep);
    test_assert(error == 0);

    return error;
}

/* Remove RDEs from test process so that it won't be cleaned up by recursive cleanup */
static int remove_RDEs()
{
    int error = 0;
    error = pd_client_remove_rde(&pd_conn, ramdisk_cap_type, BADGE_SPACE_ID_NULL);
    error |= pd_client_remove_rde(&pd_conn, file_cap_type, BADGE_SPACE_ID_NULL);
    error |= pd_client_remove_rde(&pd_conn, kvstore_cap_type, BADGE_SPACE_ID_NULL);
    return error;
}

/**
 * Starts the kvstore server process
 *
 * @param kvstore_ep returns the kvstore server's ep
 * @param fs_nsid namespace ID of fs to share
 * @param kvstore_pd  returns the pd resource for the kvstore process
 */
int start_kvstore_server(seL4_CPtr *kvstore_ep, gpi_space_id_t fs_nsid, pd_client_context_t *kvstore_pd)
{
    int error;

    gpi_obj_id_t kvstore_id;
    error = start_resource_server_pd(sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME), fs_nsid,
                                     KVSTORE_SERVER_APP, kvstore_pd, &kvstore_id);

    /* get the kvstore EP from RDE */
    kvstore_cap_type = sel4gpi_get_resource_type_code(KVSTORE_RESOURCE_NAME);
    test_assert(kvstore_cap_type != GPICAP_TYPE_NONE);
    *kvstore_ep = sel4gpi_get_rde(kvstore_cap_type);
    test_assert(*kvstore_ep != seL4_CapNull);
err_goto:
    return error;
}

/**
 * Starts the hello test process that accesses kvstore
 *
 * @param kvstore_mode the operating mode of the hello_kvstore app
 * @param parent_ep test process ep to listen for test results
 * @param kvstore_ep ep to use for remote kvstore (optional)
 * @param hello_pd returns the pd resource for the hello process
 * @param fs_nsid namespace ID of fs to share
 */
int start_hello_kvstore(kvstore_mode_t kvstore_mode,
                        ep_client_context_t parent_ep,
                        seL4_CPtr kvstore_ep,
                        pd_client_context_t *hello_pd,
                        gpi_space_id_t fs_nsid)
{
    int error;

    // Setup the hello PD's args
    int argc = 2;
    seL4_Word args[argc];

    sel4gpi_runnable_t runnable = {0};
    pd_config_t *cfg = sel4gpi_configure_process(HELLO_KVSTORE_APP, DEFAULT_STACK_PAGES, DEFAULT_HEAP_PAGES, &runnable);
    test_assert(cfg != NULL);

    *hello_pd = runnable.pd;

    // Copy the parent ep
    error = pd_client_send_cap(hello_pd, parent_ep.ep, &args[0]);
    test_assert(error == 0);

    args[1] = kvstore_mode;

    // Copy the kvstore ep, if applicable
    if (kvstore_mode == SEPARATE_PROC)
    {
        sel4gpi_add_rde_config(cfg, kvstore_cap_type, BADGE_SPACE_ID_NULL);
    }

    // Share an FS RDE
    sel4gpi_add_rde_config(cfg, sel4gpi_get_resource_type_code(FILE_RESOURCE_TYPE_NAME), fs_nsid);
    test_assert(error == 0);

    // Share necessary RDEs to start threads
    if (kvstore_mode == SEPARATE_THREAD)
    {
        sel4gpi_add_rde_config(cfg, GPICAP_TYPE_EP, BADGE_SPACE_ID_NULL);
        sel4gpi_add_rde_config(cfg, GPICAP_TYPE_PD, BADGE_SPACE_ID_NULL);
        sel4gpi_add_rde_config(cfg, GPICAP_TYPE_CPU, BADGE_SPACE_ID_NULL);
    }

    // share the ADS RDE if we're to make new ADSes
    if (kvstore_mode == SEPARATE_ADS)
    {
        sel4gpi_add_rde_config(cfg, GPICAP_TYPE_ADS, BADGE_SPACE_ID_NULL);
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
    error = start_hello_kvstore(SAME_THREAD, self_ep, 0, &hello_pd, BADGE_SPACE_ID_NULL);

    test_error_eq(remove_RDEs(), 0);

    /* Wait for test result */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    extract_model(&pd_conn);

    /* Cleanup servers */
    test_error_eq(maybe_terminate_pd(&hello_pd), 0);
    test_error_eq(maybe_terminate_pd(&fs_pd), 0);
    test_error_eq(maybe_terminate_pd(&ramdisk_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIKV001, "Test kvstore with app and lib in the same PD, same ADS", test_kvstore_lib_in_same_pd, true)

int test_kvstore_lib_in_diff_pd(env_t env)
{
    int error;

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    error = setup(env);
    test_assert(error == 0);

    /* Start the kvstore PD */
    pd_client_context_t kvstore_pd;
    seL4_CPtr kvstore_ep;
    error = start_kvstore_server(&kvstore_ep, BADGE_SPACE_ID_NULL, &kvstore_pd);
    test_assert(error == 0);

    /* Start the app PD */
    pd_client_context_t hello_pd;
    error = start_hello_kvstore(SEPARATE_PROC, self_ep, kvstore_ep, &hello_pd, BADGE_SPACE_ID_NULL);

    test_error_eq(remove_RDEs(), 0);

    /* Wait for test result */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    extract_model(&pd_conn);

    /* Cleanup servers */
    test_error_eq(maybe_terminate_pd(&hello_pd), 0);
    test_error_eq(maybe_terminate_pd(&kvstore_pd), 0);
    test_error_eq(maybe_terminate_pd(&fs_pd), 0);
    test_error_eq(maybe_terminate_pd(&ramdisk_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIKV002, "Test kvstore with app and lib in different PDs, same FS, same NS", test_kvstore_lib_in_diff_pd, true)

int test_kvstore_diff_namespace(env_t env)
{
    int error;

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    error = setup(env);
    test_assert(error == 0);

    /* Create the FS namespaces */
    seL4_CPtr fs_ep = sel4gpi_get_rde(file_cap_type);
    gpi_space_id_t nsid_1, nsid_2;

    error = xv6fs_client_new_ns(&nsid_1);
    test_assert(error == 0);

    error = xv6fs_client_new_ns(&nsid_2);
    test_assert(error == 0);

    /* Start the kvstore PD */
    seL4_CPtr kvstore_ep;
    pd_client_context_t kvstore_pd;
    error = start_kvstore_server(&kvstore_ep, nsid_1, &kvstore_pd);
    test_assert(error == 0);

    /* Start the app PD */
    pd_client_context_t hello_pd;
    error = start_hello_kvstore(SEPARATE_PROC, self_ep, kvstore_ep, &hello_pd, nsid_2);
    test_assert(error == 0);

    test_error_eq(remove_RDEs(), 0);

    /* Wait for test result */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    extract_model(&pd_conn);

    /* Cleanup PDs */
    test_error_eq(maybe_terminate_pd(&hello_pd), 0);
    test_error_eq(maybe_terminate_pd(&kvstore_pd), 0);
    test_error_eq(maybe_terminate_pd(&fs_pd), 0);
    test_error_eq(maybe_terminate_pd(&ramdisk_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIKV003, "Test app and lib with same FS, different namespace", test_kvstore_diff_namespace, true)

int test_kvstore_diff_fs(env_t env)
{
    int error;

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    error = setup(env);
    test_assert(error == 0);

    /* Start second fs server process */
    error = start_xv6fs_pd(ramdisk_id, &fs_2_pd, &fs_2_id);
    test_assert(error == 0);

    /* Start the kvstore PD */
    seL4_CPtr kvstore_ep;
    pd_client_context_t kvstore_pd;
    error = start_kvstore_server(&kvstore_ep, BADGE_SPACE_ID_NULL, &kvstore_pd);
    test_assert(error == 0);

    /* Start the app PD */
    pd_client_context_t hello_pd;
    error = start_hello_kvstore(SEPARATE_PROC, self_ep, kvstore_ep, &hello_pd, fs_2_id);
    test_assert(error == 0);

    test_error_eq(remove_RDEs(), 0);

    /* Wait for test result */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    extract_model(&pd_conn);

    /* Cleanup PDs */
    test_error_eq(maybe_terminate_pd(&hello_pd), 0);
    test_error_eq(maybe_terminate_pd(&kvstore_pd), 0);
    test_error_eq(maybe_terminate_pd(&fs_pd), 0);
    test_error_eq(maybe_terminate_pd(&fs_2_pd), 0);
    test_error_eq(maybe_terminate_pd(&ramdisk_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIKV004, "Test app and lib with different file systems", test_kvstore_diff_fs, true)

int test_kvstore_lib_same_pd_diff_ads(env_t env)
{
    int error;

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    error = setup(env);
    test_assert(error == 0);

    // /* Start the combined app/lib PD */
    pd_client_context_t hello_pd;
    error = start_hello_kvstore(SEPARATE_ADS, self_ep, 0, &hello_pd, BADGE_SPACE_ID_NULL);

    test_error_eq(remove_RDEs(), 0);

    /* Wait for test result */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    extract_model(&pd_conn);

    /* Cleanup PDs */
    test_error_eq(maybe_terminate_pd(&hello_pd), 0);
    test_error_eq(maybe_terminate_pd(&fs_pd), 0);
    test_error_eq(maybe_terminate_pd(&ramdisk_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIKV005, "Test kvstore with app and lib in the same PD, different ADS", test_kvstore_lib_same_pd_diff_ads, true)

int test_kvstore_diff_threads(env_t env)
{
    int error;

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    error = setup(env);
    test_assert(error == 0);

    /* Start the combined app/lib PD */
    pd_client_context_t hello_pd;
    error = start_hello_kvstore(SEPARATE_THREAD, self_ep, 0, &hello_pd, BADGE_SPACE_ID_NULL);

    test_error_eq(remove_RDEs(), 0);

    /* Wait for test result */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    extract_model(&pd_conn);

    /* Cleanup PDs */
    test_error_eq(maybe_terminate_pd(&hello_pd), 0);
    test_error_eq(maybe_terminate_pd(&fs_pd), 0);
    test_error_eq(maybe_terminate_pd(&ramdisk_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIKV006, "Test kvstore with app and lib in the same PD, different threads", test_kvstore_diff_threads, true)

int test_kvstore_two_sets(env_t env)
{
    int error;

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    error = setup(env);
    test_assert(error == 0);

    /* Start the kvstore PD 1 */
    seL4_CPtr kvstore_ep_1;
    pd_client_context_t kvstore_pd_1;
    error = start_kvstore_server(&kvstore_ep_1, BADGE_SPACE_ID_NULL, &kvstore_pd_1);
    test_assert(error == 0);

    /* Start the kvstore PD 2 */
    seL4_CPtr kvstore_ep_2;
    pd_client_context_t kvstore_pd_2;
    error = start_kvstore_server(&kvstore_ep_2, BADGE_SPACE_ID_NULL, &kvstore_pd_2);
    test_assert(error == 0);

    /* Start the app PD 1 */
    pd_client_context_t hello_pd_1;
    error = start_hello_kvstore(SEPARATE_PROC, self_ep, kvstore_ep_1, &hello_pd_1, BADGE_SPACE_ID_NULL);

    /* Start the app PD 2 */
    pd_client_context_t hello_pd_2;
    error = start_hello_kvstore(SEPARATE_PROC, self_ep, kvstore_ep_2, &hello_pd_2, BADGE_SPACE_ID_NULL);

    /* Wait for test result 1 */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    test_error_eq(remove_RDEs(), 0);

    /* Wait for test result 2 */
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    extract_model(&pd_conn);

    /* Cleanup PDs */
    test_error_eq(maybe_terminate_pd(&hello_pd_1), 0);
    test_error_eq(maybe_terminate_pd(&hello_pd_2), 0);
    test_error_eq(maybe_terminate_pd(&kvstore_pd_1), 0);
    test_error_eq(maybe_terminate_pd(&kvstore_pd_2), 0);
    test_error_eq(maybe_terminate_pd(&fs_pd), 0);
    test_error_eq(maybe_terminate_pd(&ramdisk_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIKV007, "Test kvstore with app and lib in different PDs, 2 sets of each", test_kvstore_two_sets, true)

int test_kvstore_lib_in_diff_pd_crash(env_t env)
{
    int error;

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    error = setup(env);
    test_assert(error == 0);

    /* Create the FS namespaces */
    seL4_CPtr fs_ep = sel4gpi_get_rde(file_cap_type);
    gpi_space_id_t nsid_1, nsid_2;

    error = xv6fs_client_new_ns(&nsid_1);
    test_assert(error == 0);

    error = xv6fs_client_new_ns(&nsid_2);
    test_assert(error == 0);

    /* Start the kvstore PD */
    pd_client_context_t kvstore_pd;
    seL4_CPtr kvstore_ep;
    error = start_kvstore_server(&kvstore_ep, nsid_1, &kvstore_pd);
    test_assert(error == 0);

    /* Start the app PD */
    pd_client_context_t hello_pd;
    error = start_hello_kvstore(SEPARATE_PROC, self_ep, kvstore_ep, &hello_pd, nsid_2);

    test_error_eq(remove_RDEs(), 0);

    /* Wait for test result */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    extract_model(&pd_conn);

    /* Crash the ramdisk */
    printf("Crashing the ramdisk\n");
    error = pd_client_terminate(&ramdisk_pd);
    test_assert(error == 0);

    extract_model(&pd_conn);

    /* Cleanup servers */
    test_error_eq(maybe_terminate_pd(&hello_pd), 0);
    test_error_eq(maybe_terminate_pd(&kvstore_pd), 0);
    test_error_eq(maybe_terminate_pd(&fs_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIKV008,
                "Test kvstore with app and lib in different PDs, same FS, different NS: ramdisk crashes",
                test_kvstore_lib_in_diff_pd_crash, true)