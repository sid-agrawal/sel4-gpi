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

#define XV6FS_C "xv6fs Client: "


/**
 * Initializes a process as a xv6fs client
 *
 * CAUTION:
 * All vka_t and vspace_t instances passed to this library by
 * reference must remain functional for future xv6fs requests.
 *
 * @param client_vka Initialized vka_t for the client process
 * @param client_vspace Initialized vspace_t for client process
 * @param server_ep_cap Server thread's endpoint cap.
 * @return seL4_Error value.
 */
seL4_Error
xv6fs_client_init(vka_t *client_vka,
                  vspace_t *client_vspace,
                  seL4_CPtr server_ep_cap);

/*
Context of the client
*/
typedef struct _xv6fs_client_context
{
    vka_t *client_vka;
    vspace_t *client_vspace;
    seL4_CPtr server_ep_cap;
    void *shared_mem;
} xv6fs_client_context_t;
