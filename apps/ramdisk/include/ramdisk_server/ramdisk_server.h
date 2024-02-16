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

#include <ramdisk_shared.h>

struct _ads_client_context;
typedef struct _ads_client_context ads_client_context_t;

struct _mo_client_context;
typedef struct _mo_client_context mo_client_context_t;

struct _pd_init_data;
typedef struct _pd_init_data pd_init_data_t;

#define RAMDISK_SERVER_DEFAULT_PRIORITY (seL4_MaxPrio - 100)

typedef struct _pd_env
{
    simple_t simple;
    vka_t vka;
    vspace_t vspace;
} pd_env_t;

/**
 * Starts the ramdisk in the current thread
 * Assumes the ramdisk is started in a new PD
 *
 * @param parent_ep Endpoint of the parent process
 * @return 0 on successful exit, nonzero otherwise
 */
int ramdisk_server_start(vka_t *vka,
                         pd_init_data_t *init_data,
                         seL4_CPtr parent_ep);

/**
 * Starts the ramdisk as a thread within the current PD
 */
seL4_Error
ramdisk_server_spawn_thread(simple_t *parent_simple,
                            vka_t *parent_vka,
                            vspace_t *parent_vspace,
                            seL4_CPtr gpi_ep,
                            seL4_CPtr parent_ep,
                            seL4_CPtr ads_ep,
                            uint8_t priority);

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
    vka_t *server_vka;

    // Endpoints of other pds
    seL4_CPtr gpi_server;
    ads_client_context_t *ads_conn;
    seL4_CPtr parent_ep; // Used once to tell parent that we have started

    // The server listens on this endpoint.
    seL4_CPtr server_ep;

    // Memory for ramdisk
    void *ramdisk_buf;
    mo_client_context_t *ramdisk_mo;

    // Data structure of ramdisk blocks
    ramdisk_block_node_t *free_blocks;

    seL4_CPtr mcs_reply; // How to use this?
} ramdisk_server_context_t;

/**
 * Internal library function: acts as the main() for the server thread.
 **/
int ramdisk_server_main(void);

ramdisk_server_context_t *get_ramdisk_server(void);