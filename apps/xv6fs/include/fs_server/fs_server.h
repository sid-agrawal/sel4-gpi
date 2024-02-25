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
#include <ramdisk_client.h>
#include <fs_shared.h>

#define XV6FS_S "xv6fs Server: "
#define XV6FS_SERVER_DEFAULT_PRIORITY (seL4_MaxPrio - 100)

struct _ads_client_context;
typedef struct _ads_client_context ads_client_context_t;

struct _pd_client_context;
typedef struct _pd_client_context pd_client_context_t;

/** @file API for allowing a thread to act as the parent to a xv6fs server
 * thread.
 *
 * Provides the APIs for spawning the server thread.
 */

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
 * @return seL4_Error value.
 */
seL4_Error
xv6fs_server_spawn_thread(simple_t *parent_simple,
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
    simple_t *server_simple;
    vka_t *server_vka;
    seL4_CPtr server_cspace;
    vspace_t *server_vspace;
    sel4utils_thread_t server_thread;
    int (*next_slot)(seL4_CPtr *);
    int (*badge_ep)(seL4_Word, seL4_CPtr *);

    // RDEs and other EPs
    seL4_CPtr parent_ep;
    seL4_CPtr gpi_ep;
    seL4_CPtr rd_ep;
    ads_client_context_t *ads_conn;
    pd_client_context_t *pd_conn;

    // The server listens on this endpoint.
    seL4_CPtr server_ep;

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
void xv6fs_server_main(void);

xv6fs_server_context_t *get_xv6fs_server(void);