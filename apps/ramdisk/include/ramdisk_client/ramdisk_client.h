
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
    cspacepath_t badged_server_ep_cspath;
} ramdisk_client_context_t;

/**
 * @brief Allocate a new block from ramdisk
 *
 * Requests a new block from the ramdisk server, returning
 * a connection object for the new block on success.
 *
 * @param server_ep_cap Well known server endpoint cap.
 * @param client_vka client's cka for allocating memory.
 * @param ret_conn client's connection object
 * @return int 0 on success, -1 on failure.
 */
int ramdisk_client_alloc_block(seL4_CPtr server_ep_cap,
                               vka_t *client_vka,
                               ramdisk_client_context_t *ret_conn);

/**
 * @brief Read an allocated block from ramdisk
 *
 * @param conn client connection object
 * @param mo memory to read block into, should be size >= RAMDISK_BLOCK_SIZE
 * @return int 0 on success, -1 on failure.
 */
int ramdisk_client_read(ramdisk_client_context_t *conn, mo_client_context_t *mo);

/**
 * @brief Write an allocated block to ramdisk
 *
 * @param conn client connection object
 * @param mo memory to write into block, should be size >= RAMDISK_BLOCK_SIZE
 * @return int 0 on success, -1 on failure.
 */
int ramdisk_client_write(ramdisk_client_context_t *conn, mo_client_context_t *mo);

/**
 * Get the block size of the ramdisk
*/
uint64_t get_ramdisk_block_size();