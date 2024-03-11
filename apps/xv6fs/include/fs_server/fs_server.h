/**
 * @file xv6fs.h
 * @author
 * @brief Implements functions needed by a parent to interact with the xv6fs server.
 * @version 0.1
 * @date 2024-01-25
 */

#pragma once

#include <sel4/sel4.h>
#include <sel4/types.h>

#include <sel4gpi/resource_server_utils.h>
#include <ramdisk_client.h>

#include <fs_shared.h>

#define XV6FS_S "xv6fs Server: "
#define XV6FS_SERVER_DEFAULT_PRIORITY (seL4_MaxPrio - 100)

struct _ads_client_context;
typedef struct _ads_client_context ads_client_context_t;

struct _pd_client_context;
typedef struct _pd_client_context pd_client_context_t;

/* Per-client context maintained by the server. */
typedef struct _fs_registry_entry
{
    struct file *file;
    uint32_t count; // There can be more than one cap to this object
    struct _fs_registry_entry *next;

} fs_registry_entry_t;

/*
Context of the server
*/
typedef struct _xv6fs_server_context
{
    // Generic resource server context
    resource_server_context_t gen;

    // Other EPs
    seL4_CPtr rd_ep;

    // Internal data
    int registry_n_entries;
    fs_registry_entry_t *client_registry;

    // Temporary fields for naive implementation
    mo_client_context_t *shared_mem;
    void *shared_mem_vaddr;
    ramdisk_client_context_t naive_blocks[FS_SIZE];
} xv6fs_server_context_t;

/**
 * To be run once at the start of the fs server
 */
int xv6fs_init();

/**
 * To handle client requests to the fs server
*/
seL4_MessageInfo_t xv6fs_request_handler(seL4_MessageInfo_t tag, seL4_Word sender_badge, seL4_CPtr cap);

xv6fs_server_context_t *get_xv6fs_server(void);