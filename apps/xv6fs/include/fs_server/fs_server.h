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

/** Spawns the xv6fs server thread. Server thread is spawned within the VSpace and
 *  CSpace of the thread that spawned it.
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
 * @param rd_ep Endpoint to the ramdisk server
 * @param parent_ep Endpoint to communicate with parent
 * @param ads_ep Initialized ADS connection
 * @param pd_ep Initialized PD connection
 * @param priority Server thread's priority.
 * @return int 0 on success, -1 otherwise
 */
int xv6fs_server_spawn_thread(simple_t *parent_simple,
                              vka_t *parent_vka,
                              vspace_t *parent_vspace,
                              seL4_CPtr gpi_ep,
                              seL4_CPtr rd_ep,
                              seL4_CPtr parent_ep,
                              seL4_CPtr ads_ep,
                              seL4_CPtr pd_ep,
                              uint8_t priority);

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
 * Internal library function: acts as the main() for the server thread.
 **/
int xv6fs_server_main(void);

xv6fs_server_context_t *get_xv6fs_server(void);