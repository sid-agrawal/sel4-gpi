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
#define MAX_CLIENT_ID 32

/* Context of the server */

// Linked list node represents a block or block range
typedef struct _ramdisk_block_node
{
    gpi_obj_id_t blockno;
    uint32_t n_blocks;
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

    // Store per-client page for shared mem
    void *shared_mem[MAX_CLIENT_ID];
} ramdisk_server_context_t;

/**
 * To be run once at the start of the ramdisk server
 */
int ramdisk_init();

/**
 * To handle client requests to the ramdisk server
 */
void ramdisk_request_handler(void *msg_p,
                             void *msg_reply_p,
                             seL4_Word sender_badge,
                             seL4_CPtr cap,
                             bool *need_new_recv_cap);

/**
 * To handle root task requests to the ramdisk server
 */
int ramdisk_work_handler(PdWorkReturnMessage *work);

ramdisk_server_context_t *get_ramdisk_server(void);