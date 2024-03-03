/**
 * @file API for allowing a thread to act as the parent to a ramdisk server
 * thread.
 *
 * Provides the APIs for spawning the server thread.
 */

#pragma once

#include <stdint.h>

#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4gpi/resource_server_utils.h>

#include <ramdisk_shared.h>

#define RAMDISK_SERVER_DEFAULT_PRIORITY (seL4_MaxPrio - 100)

/**
 * Spawns the ramdisk server thread.
 * Server thread is spawned within the VSpace and
 * CSpace of the thread that spawned it.
 *
 * Note: mainly for use from the root task, which does
 * not have a PD cap. Within a regular PD, use resource_server_start
 *
 * CAUTION:
 * All vka_t, vspace_t, and simple_t instances passed to this library by
 * reference must remain functional throughout the lifetime of the server.
 *
 * @param parent_simple Initialized simple_t for the parent process that is
 *                      spawning the server thread.
 * @param parent_vka Initialized vka_t for the parent process that is spawning
 *                   the server thread.
 * @param parent_vspace Initialized vspace_t for the parent process that is
 *                      spawning the server thread.
 * @param gpi_ep Endpoint to the gpi server
 * @param parent_ep Endpoint to communicate with parent
 * @param ads_ep Initialized ADS connection
 * @param priority Server thread's priority.
 * @return int 0 on success, -1 otherwise
 */
int ramdisk_server_spawn_thread(simple_t *parent_simple,
                                vka_t *parent_vka,
                                vspace_t *parent_vspace,
                                seL4_CPtr gpi_ep,
                                seL4_CPtr parent_ep,
                                seL4_CPtr ads_ep,
                                uint8_t priority);

/* Context of the server */

// Linked list node represents a block or block range
typedef struct _ramdisk_block_node
{
    uint64_t blockno;
    uint64_t n_blocks;
    struct _ramdisk_block_node *next;
} ramdisk_block_node_t;

typedef struct _ramdisk_server_context
{
    // Generic resource server context
    resource_server_context_t gen;

    // Memory for ramdisk
    void *ramdisk_buf;
    mo_client_context_t *ramdisk_mo;

    // Data structure of ramdisk blocks
    ramdisk_block_node_t *free_blocks;
} ramdisk_server_context_t;

/**
 * Internal library function: acts as the main() for the server thread.
 **/
int ramdisk_server_main(void);

ramdisk_server_context_t *get_ramdisk_server(void);