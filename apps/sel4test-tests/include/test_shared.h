#pragma once

#include <sel4gpi/pd_clientapi.h>
#include <kvstore_client.h>

/**
 * @file Define some functions shared between tests
 */

// Use to warn of an error that does not necessarily mean test failure
// This is often used for terminating PDs that may have been cleaned up already
#define WARN_IF_ERR(error, msg)        \
    do                                 \
    {                                  \
        if (error)                     \
        {                              \
            printf("WARN: %s\n", msg); \
        }                              \
    } while (0)

/**
 * If model extraction is enabled, extract the whole model state
 */
void extract_model(pd_client_context_t *pd_conn);

/**
 * Try to terminate a PD that may have already been cleaned up
 * If the termination fails due to the PD already being cleaned up, just print a warning
 * If there is another error, return the error
 *
 * @param pd_conn the PD to maybe terminate
 */
int maybe_terminate_pd(pd_client_context_t *pd_conn);

/** BENCHMARKS **/

/**
 * Initializes sel4bench, and issues some IPC calls for warmup
 */
void benchmark_init(env_t env);

/**
 * Send an seL4_Call to root task, this is meant to warmup IPCs
 */
static int benchmark_ipc_rt(env_t env);

/** TOY RESOURCE SERVERS **/

#define TOY_BLOCK_SERVER_RESOURCE_TYPE "TOY_BLOCK"
#define TOY_FILE_SERVER_RESOURCE_TYPE "TOY_FILE"
#define TOY_DB_SERVER_RESOURCE_TYPE "TOY_DB"

// This needs to be the same as the definition in hello-cleanup/toy_server.h
typedef enum _hello_cleanup_mode
{
    HELLO_CLEANUP_TOY_BLOCK_SERVER_MODE, ///< Process will serve toy_blocks
    HELLO_CLEANUP_TOY_FILE_SERVER_MODE,  ///< Process will serve toy_file
    HELLO_CLEANUP_TOY_DB_SERVER_MODE,    ///< Process will serve toy_db
    HELLO_CLEANUP_TOY_BLOCK_CLIENT_MODE, ///< Process will request toy_blocks
    HELLO_CLEANUP_TOY_FILE_CLIENT_MODE,  ///< Process will request toy_file
    HELLO_CLEANUP_TOY_DB_CLIENT_MODE,    ///< Process will request toy_DB
    HELLO_CLEANUP_NOTHING_MODE,          ///< Process will do nothing
} hello_cleanup_mode_t;

/**
 * Starts the hello-cleanup process
 *
 * @param mode the mode for hello to run in
 * @param n_client_requests number of requests for clients to make
 * @param ep the endpoint to send to the process for reporting test results
 * @param hello_pd  returns the pd resource for the hello process
 * @return 0 on success, error otherwise
 */
int start_toy_cleanup_process(hello_cleanup_mode_t mode, uint32_t n_client_requests,
                              ep_client_context_t *ep, pd_client_context_t *hello_pd);

/* KVSTORE SAMPLE PDs */

/**
 * Starts the kvstore server process
 *
 * @param kvstore_ep returns the kvstore server's ep
 * @param fs_nsid namespace ID of fs to share
 * @param kvstore_pd  returns the pd resource for the kvstore process
 */
int start_kvstore_server(seL4_CPtr *kvstore_ep, gpi_space_id_t fs_nsid, pd_client_context_t *kvstore_pd);

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
                        gpi_space_id_t fs_nsid);