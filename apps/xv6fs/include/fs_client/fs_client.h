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

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/pd_clientapi.h>

#define XV6FS_C "xv6fs Client: "

/**
 * Starts the fs server in a new process
 *
 * @param vka vka to use while creating the process
 * @param gpi_ep endpoint of the gpi server
 * @param rd_ep endpoint of the ramdisk server
 * @param fs_ep outputs the ep of the new fs server
 * @return 0 on success, or -1 otherwise
 */
int start_xv6fs_pd(vka_t *vka,
                   seL4_CPtr gpi_ep,
                   seL4_CPtr rd_ep,
                   seL4_CPtr rde_pd_cap,
                   seL4_CPtr *fs_ep);

/**
 * Initializes a process as a xv6fs client
 * Relevant libc functions will be overridden in the current process
 *
 * @param client_vka Initialized vka_t for the client process
 * @param fs_ep xv6fs server's endpoint
 * @param ads_conn
 * @param pd_conn
 * @return seL4_Error value.
 */
seL4_Error
xv6fs_client_init(vka_t *client_vka,
                  seL4_CPtr fs_ep,
                  seL4_CPtr gpi_ep,
                  seL4_CPtr ads_ep,
                  seL4_CPtr pd_ep);

/**
 * Get the file resource for a given file descriptor
 * @param fd fd returned by libc open in a xv6fs client process
 * @param file_ep returns the badged endpoint to the file server
*/
int xv6fs_client_get_file(int fd, seL4_CPtr *file_ep);

/*
Context of the client for a single file
*/
typedef struct _xv6fs_client_context
{
    cspacepath_t badged_server_ep_cspath;
    uint64_t offset;
    uint64_t flags; // For fcntl, can we remove?
} xv6fs_client_context_t;

/*
Context of the client for this process
*/
typedef struct _global_xv6fs_client_context_t
{
    vka_t *client_vka;
    seL4_CPtr fs_ep;
    seL4_CPtr gpi_ep;
    ads_client_context_t *ads_conn;
    pd_client_context_t *pd_conn;
    int (*next_slot)(seL4_CPtr *);

    // Temporary fields for naive implementation
    mo_client_context_t *shared_mem;
    void *shared_mem_vaddr;
} global_xv6fs_client_context_t;