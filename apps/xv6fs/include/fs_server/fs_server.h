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

#define XV6FS_S "xv6fs Server: "
#define XV6FSK_SERVER_DEFAULT_PRIORITY (seL4_MaxPrio - 100)

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
 * @param priority Server thread's priority.
 * @param server_endpoint Server thread's endpoint cap.
 * @return seL4_Error value.
 */
seL4_Error
xv6fs_server_spawn_thread(simple_t *parent_simple, vka_t *parent_vka,
                          vspace_t *parent_vspace,
                          seL4_CPtr gpi_ep,
                          seL4_CPtr rd_ep,
                          uint8_t priority,
                          seL4_CPtr *server_ep_cap);

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

    // RDEs
    seL4_CPtr gpi_ep;
    seL4_CPtr rd_ep;

    // Generic block read/write functions
    int (*block_read)(uint, void *);
    int (*block_write)(uint, void *);

    // The server listens on this endpoint.
    vka_object_t server_ep_obj;

    // Shared memory with client
    // NOTE: only supports one client at this time
    void *shared_mem;
} xv6fs_server_context_t;

/**
 * Internal library function: acts as the main() for the server thread.
 **/
void xv6fs_server_main(void);

xv6fs_server_context_t *get_xv6fs_server(void);