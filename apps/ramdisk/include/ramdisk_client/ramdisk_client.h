
/**
 * @file API for a client process to communicate with the ramdisk server
 */

#pragma once

#include <stdint.h>

#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4gpi/mo_clientapi.h>

#include <ramdisk_shared.h>

/*
Context of the client
*/
typedef struct _ramdisk_client_context
{
    seL4_CPtr ep;

    // Needed only for RR dump
    gpi_space_id_t space_id;
    gpi_obj_id_t res_id;
} ramdisk_client_context_t;

/**
 * Starts the ramdisk server in a new process
 *
 * @param ramdisk_pd returns the PD resource of the new ramdisk server
 * @param ramdisk_id returns the resource space ID of the ramdisk
 * @return 0 on success, or -1 otherwise
 */
int start_ramdisk_pd(pd_client_context_t *ramdisk_pd,
                     gpi_obj_id_t *ramdisk_id);

/**
 * @brief
 * Establish shared memory with ramdisk
 * Shared memory will be used for future read/write calls
 *
 * @param server_ep_cap raw ramdisk ep
 * @param mo memory to share, should be size >= RAMDISK_BLOCK_SIZE
 * @return int 0 on success, error code otherwise
 */
int ramdisk_client_bind(seL4_CPtr server_ep_cap,
                        mo_client_context_t *mo);

/**
 * @brief
 * Remove shared memory from ramdisk
 *
 * @param server_ep_cap raw ramdisk ep
 * @return int 0 on success, error code otherwise
 */
int ramdisk_client_unbind(seL4_CPtr server_ep_cap);

/**
 * @brief Allocate a new block from ramdisk
 *
 * Requests a new block from the ramdisk server, returning
 * a connection object for the new block on success.
 *
 * @param server_ep_cap Well known server endpoint cap.
 * @param ret_conn client's connection object
 * @return int 0 on success, error code otherwise
 */
int ramdisk_client_alloc_block(seL4_CPtr server_ep_cap,
                               ramdisk_client_context_t *ret_conn);

/**
 * @brief Return a block to the ramdisk
 *
 * @param conn connection for the block to free
 * @return int 0 on success, error code otherwise
 */
int ramdisk_client_free_block(ramdisk_client_context_t *conn);

/**
 * @brief Read an allocated block from ramdisk
 * Uses the shared memory as set in ramdisk_client_bind
 *
 * @param conn client connection object
 * @return int 0 on success, error code otherwise
 */
int ramdisk_client_read(ramdisk_client_context_t *conn);

/**
 * @brief Write an allocated block to ramdisk
 * Uses the shared memory as set in ramdisk_client_bind
 *
 * @param conn client connection object
 * @return int 0 on success, error code otherwise
 */
int ramdisk_client_write(ramdisk_client_context_t *conn);

/**
 * Get the block size of the ramdisk
 */
uint64_t get_ramdisk_block_size();