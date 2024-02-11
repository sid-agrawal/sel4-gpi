/**
 * @file ramdisk.h
 * @author
 * @brief Implements functions needed by a parent to interact with the ramdisk server.
 * @version 0.1
 * @date 2024-01-25
 */

#pragma once

#include <stdint.h>

#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4gpi/gpi_server.h>
#include <sel4gpi/ads_clientapi.h>

#define RAMDISK_S "RamDisk Server: "
#define RAMDISK_SERVER_DEFAULT_PRIORITY (seL4_MaxPrio - 100)

/* ramdisk configuration */
#define RAMDISK_BLOCK_SIZE SIZE_BITS_TO_BYTES(seL4_PageBits) // Block size for the ramdisk
#define RAMDISK_SIZE_BITS 21                                 // Size of total ramdisk
#define RAMDISK_SIZE_BYTES SIZE_BITS_TO_BYTES(RAMDISK_SIZE_BITS)

/** @file API for allowing a thread to act as the parent to a ramdisk server
 * thread.
 *
 * Provides the APIs for spawning the server thread.
 */

/** Spawns the ramdisk server thread. Server thread is spawned within the VSpace and
 *  CSpace of the thread that spawned it.
 *
 * CAUTION:
 * All vka_t, vpsace_t, and simple_t instances passed to this library by
 * reference must remain functional throughout the lifetime of the server.
 *
 * @param parent_simple Initialized simple_t for the parent process that is
 *                      spawning the server thread.
 * @param parent_vka Initialized vka_t for the parent process that is spawning
 *                   the server thread.
 * @param parent_vspace Initialized vspace_t for the parent process that is
 *                      spawning the server thread.
 * @param gpi_server Initialized gpi server connection for the parent process that is
 *                      spawning the server thread
 * @param priority Server thread's priority.
 * @param server_endpoint Server thread's endpoint cap.
 * @return seL4_Error value.
 */
seL4_Error
ramdisk_server_spawn_thread(simple_t *parent_simple, vka_t *parent_vka,
                            vspace_t *parent_vspace, seL4_CPtr gpi_server,
                            uint8_t priority,
                            seL4_CPtr *server_ep_cap);

/*
Context of the server
*/

// Linked list node represents a block or block range
typedef struct _ramdisk_block_node
{
    uint64_t blockno;
    uint64_t n_blocks;
    struct _ramdisk_block_node *next;
} ramdisk_block_node_t;

typedef struct _ramdisk_server_context
{
    simple_t *server_simple;
    vka_t *server_vka;
    seL4_CPtr server_cspace;
    vspace_t *server_vspace;
    sel4utils_thread_t server_thread;
    seL4_CPtr gpi_server;
    ads_client_context_t ads_conn;

    // The server listens on this endpoint.
    vka_object_t server_ep_obj;

    // Memory for ramdisk
    void *ramdisk_buf;
    vka_object_t ramdisk_buf_obj;
    ramdisk_block_node_t *free_blocks;
} ramdisk_server_context_t;

/**
 * Internal library function: acts as the main() for the server thread.
 **/
void ramdisk_server_main(void);

ramdisk_server_context_t *get_ramdisk_server(void);

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