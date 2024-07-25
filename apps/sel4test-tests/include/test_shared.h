#pragma once

#include <sel4gpi/pd_clientapi.h>

/**
 * @file Define some functions shared between tests
 */

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