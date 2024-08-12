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
#include <sel4gpi/vmr_clientapi.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/gpi_rpc.h>

#include <fs_shared.h>

#define XV6FS_C "xv6fs Client: "

/**
 * Starts the fs server in a new process
 *
 * @param rd_id space ID of the ramdisk server's block space
 * @param fs_pd returns the PD resource of the new fs server
 * @param fs_id returns the resource space ID of the new fs server
 * @return 0 on success, or -1 otherwise
 */
int start_xv6fs_pd(gpi_space_id_t rd_id,
                   pd_client_context_t *fs_pd,
                   gpi_space_id_t *fs_id);

/**
 * Initializes a process as a xv6fs client
 * Relevant libc functions will be overridden in the current process
 * @return seL4_Error value.
 */
seL4_Error
xv6fs_client_init(void);

/**
 * Changes the namespace used for requests to the FS server
 * 
 * @param ns_id ID of an existing FS namespace
 * @return 0 on success, 
 *         -1 if the namespace was not found in the resource directory
*/
int xv6fs_client_set_namespace(gpi_space_id_t ns_id);

/**
 * Get the file resource for a given file descriptor
 * @param fd fd returned by libc open in a xv6fs client process
 * @param file_ep returns the badged endpoint to the file server
*/
int xv6fs_client_get_file(int fd, seL4_CPtr *file_ep);

/**
 * Links a file resource to a particular filepath
 * 
 * @param file file resource created by this filesystem, 
 *             potentially in a different namespace
 * @param path path to link the file at
*/
int xv6fs_client_link_file(seL4_CPtr file, const char *path);

/**
 * Request a new namespace from the file server
 *
 * @param ns_id returns the ID of the new NS
 * @return 0 on success, error otherwise
 */
int xv6fs_client_new_ns(gpi_space_id_t *ns_id);

/**
 * Delete a namespace from the file server
 *
 * @param ns_ep the endpoint of the namespace connection to the FS server
 * @return 0 on success, error otherwise
 */
int xv6fs_client_delete_ns(seL4_CPtr ns_ep);

/*
Context of the client for a single file
*/
typedef struct _xv6fs_client_context
{
    seL4_CPtr ep;
    uint64_t offset;
    uint64_t flags; // For fcntl, can we remove?
} xv6fs_client_context_t;

/*
Context of the client for this process
*/
typedef struct _global_xv6fs_client_context_t
{
    vka_t *client_vka;
    gpi_cap_t file_cap_type;
    gpi_space_id_t space_id;
    seL4_CPtr server_ep;

    // Shared memory frame with the file server, sent on every request
    mo_client_context_t *shared_mem;
    void *shared_mem_vaddr;
} global_xv6fs_client_context_t;
