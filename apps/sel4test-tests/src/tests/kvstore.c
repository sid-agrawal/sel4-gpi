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
#include <kvstore_client.h>
#include <kvstore_server_rpc.pb.h>

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
    // error |= pd_client_remove_rde(&pd_conn, kvstore_cap_type, BADGE_SPACE_ID_NULL);
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
    // XX-SID
    if ((kvstore_mode == SEPARATE_THREAD) ||
        (kvstore_mode == SEPARATE_THREAD_WITH_ISOLATED_STACK))
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
DEFINE_TEST_OSM(GPIKV002, "Test kvstore with app and lib in different PDs, same FS, same NS", 
    test_kvstore_lib_in_diff_pd, true)

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
DEFINE_TEST_OSM(GPIKV003, "Test app and lib with same FS, different namespace", 
    test_kvstore_diff_namespace, true)

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
DEFINE_TEST_OSM(GPIKV006, "Test kvstore with app and lib in the same PD, different threads", 
    test_kvstore_diff_threads, true)
/* Thread with isolated stack is GPIKV010*/

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

#ifdef OSM_VMM
#include <gpivmm/osm-vmm.h>

/*
    This test i.e. GPIKV009. Relies on the following:
    1. The gPA KVS_VM_SHARED_PAGE_HOST_PA being free i.e. not used by the linux kernel. 
       Note that in our setup gPA == hPA.
    2. The start of the guest RAM is mapped at 0x10200000, in the VMM's address space. 
       We have an assert for that in the VMM's setup code. 
    3. The linux is going to run the cmd "/root/proc/kv_app_in_vm 0x5f600000", after boot up. 
       This maps dev mem and uses that addr as a shared buffer. 

    Other notes:
    1. We use busy loops to synchronize
    2. The communication from the linux process to the KV Server, is relayed via the VMM. 
       We could also do this via a separate KVS client, but setting shared memory for that with the VM 
       needed some addtional plumping. Basically the we have 512MB MO, and to allow a separate PD 
       to map that one page, we need to split that MO.

*/
#define KVS_VM_SHARED_PAGE_HOST_PA 0x5f600000
static seL4_CPtr server_ep;
int get_rpc(seL4_CPtr kvstore_ep, int key, int *val)
{
    int error = 0;
    sel4gpi_rpc_env_t rpc_client = {
        .request_desc = &KvstoreMessage_msg,
        .reply_desc = &KvstoreReturnMessage_msg,
    };
    KvstoreMessage request = {
        .magic = KVSTORE_RPC_MAGIC,
        .which_msg = KvstoreMessage_get_tag,
        .msg.set = {
            .key = key,
        }};

    KvstoreReturnMessage reply = {0};

    error = sel4gpi_rpc_call(&rpc_client, kvstore_ep, &request, 0, NULL, &reply);

    error |= reply.errorCode;

    if (error == seL4_NoError)
    {
        *val = reply.msg.get.val;
    }
    return error;
}

int set_rpc(seL4_CPtr kvstore_ep, int key, int val)
{
    int error = 0;
    sel4gpi_rpc_env_t rpc_client = {
        .request_desc = &KvstoreMessage_msg,
        .reply_desc = &KvstoreReturnMessage_msg,
    };
    // Set and get one value
    KvstoreMessage request2 = {
        .magic = KVSTORE_RPC_MAGIC,
        .which_msg = KvstoreMessage_set_tag,
        .msg.set = {
            .key = key,
            .val = val,
        }};

    KvstoreReturnMessage reply2 = {0};

    error = sel4gpi_rpc_call(&rpc_client, kvstore_ep, &request2, 0, NULL, &reply2);

    error |= reply2.errorCode;

    printf("kvstore_client_set done %d\n", request2.msg.set.val);
    return error;
}

// Shared buffer semantics
typedef enum
{
    GET,
    SET
} message_type_t;

typedef struct
{
    message_type_t cmd;
    int key;
    int value;
    int message_ready; // To be accessed with atomic ops.
    int result_ready; // To be accessed with atomic ops.
    int result;
} shared_buffer_t;

int shared_mem_setup(env_t env)
{
    /*
        Setup (i.e., find) the shared buffer, on the linux process, we do this via
        mmaping /dev/mem
    */
    uint64_t ret_vaddr = 0x10200000 + (KVS_VM_SHARED_PAGE_HOST_PA - QEMU_VM_RESERVE_PADDR);
    gpi_cap_t kvstore_cap_type = sel4gpi_get_resource_type_code(KVSTORE_RESOURCE_NAME);
    seL4_CPtr kvstore_ep = sel4gpi_get_rde(kvstore_cap_type);
    printf("KV RDE EP %d\n", kvstore_ep);

    ///////////////KVS INIT//////////
    int error = 0;
    sel4gpi_rpc_env_t rpc_client = {
        .request_desc = &KvstoreMessage_msg,
        .reply_desc = &KvstoreReturnMessage_msg,
    };
    KvstoreMessage request = {
        .magic = KVSTORE_RPC_MAGIC,
        .which_msg = KvstoreMessage_create_tag};
    KvstoreReturnMessage reply = {0};

    error = sel4gpi_rpc_call(&rpc_client, kvstore_ep, &request, 0, NULL, &reply);
    error |= reply.errorCode;
    if (error == seL4_NoError)
    {
        kvstore_ep = reply.msg.alloc.dest;
    }

    /* While loop to keep reading from the shared buffer and send to KVS Server*/
    shared_buffer_t *shared_buffer = (shared_buffer_t *)ret_vaddr;
    while (1)
    {
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        while (!__atomic_load_n(&shared_buffer->message_ready, __ATOMIC_SEQ_CST))
        {
            // Busy-wait
            sel4test_sleep(env, NS_IN_S);
        }
        printf("after while\n");
        char key_str[20];
        sprintf(key_str, "%d", shared_buffer->key);

        char value_str[20];
        if (shared_buffer->cmd == SET)
        {
            sprintf(value_str, "%d", shared_buffer->value);
        }
        else
        {
            sprintf(value_str, "NA");
        }
        // printf("Thread 2 received message: %s %s %s\n",
        //     shared_buffer->cmd == GET ? "GET" : "SET",
        //     key_str, value_str);
        __atomic_store_n(&shared_buffer->message_ready, 0, __ATOMIC_SEQ_CST);

        if (shared_buffer->cmd == SET)
        {
            error = set_rpc(kvstore_ep, shared_buffer->key, shared_buffer->value);
            test_error_eq(error, 0);
            shared_buffer->result = 0; // Indicate success
        }
        else if (shared_buffer->cmd == GET)
        {
            int val;
            error = get_rpc(kvstore_ep, shared_buffer->key, &val);
            // test_error_eq(error, 0);
            assert((error == 0) || (error == KvstoreError_KEY));
            shared_buffer->result = val;
        }

        __atomic_store_n(&shared_buffer->result_ready, 1, __ATOMIC_SEQ_CST);

        // Sleep for a random time between 1 and 2 seconds
        sel4test_sleep(env, NS_IN_S);
    }
    return 0;
}
static int start_vmm_and_guest(const char *guest_name)
{
    int error = osm_vmm_init();
    test_error_eq(error, 0);

    uint32_t guest_id = osm_new_guest(guest_name);
    test_assert(guest_id != 0);

    return error;
}

int test_kvstore_in_pd_app_in_VM(env_t env)
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

    test_error_eq(remove_RDEs(), 0);

    start_vmm_and_guest(LINUX_KERNEL_NAME);
    sel4test_sleep(env, 15 * NS_IN_S);
    shared_mem_setup(env);
    /* Wait for test result */

    while (1)
    {
        sel4test_sleep(env, 1 * NS_IN_S);
    }
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    tag = seL4_Recv(self_ep.raw_endpoint, NULL);
    error = seL4_MessageInfo_get_label(tag);
    test_assert(error == 0);

    // extract_model(&pd_conn);

    /* Cleanup servers */
    // test_error_eq(maybe_terminate_pd(&hello_pd), 0);
    test_error_eq(maybe_terminate_pd(&kvstore_pd), 0);
    test_error_eq(maybe_terminate_pd(&fs_pd), 0);
    test_error_eq(maybe_terminate_pd(&ramdisk_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIKV009, "Test kvstore with app in VM and KVS in host PD",
                test_kvstore_in_pd_app_in_VM, true)

#endif
int test_kvstore_diff_threads_with_isolated_stacks(env_t env)
{
    int error;

    printf("------------------STARTING TEST: %s------------------\n", __func__);

    error = setup(env);
    test_assert(error == 0);

    /* Start the combined app/lib PD */
    pd_client_context_t hello_pd;
    error = start_hello_kvstore(SEPARATE_THREAD_WITH_ISOLATED_STACK,
                                self_ep, 0, &hello_pd, BADGE_SPACE_ID_NULL);
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
    test_error_eq(maybe_terminate_pd(&fs_pd), 0);
    test_error_eq(maybe_terminate_pd(&ramdisk_pd), 0);

    printf("------------------ENDING: %s------------------\n", __func__);
    return sel4test_get_result();
}
DEFINE_TEST_OSM(GPIKV010, 
    "Test kvstore with app and lib in the same PD, "
    "different threads with isolated stacks", 
    test_kvstore_diff_threads_with_isolated_stacks, true)