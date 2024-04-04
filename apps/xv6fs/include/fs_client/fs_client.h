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

#include <fs_shared.h>

#define XV6FS_C "xv6fs Client: "

#define FS_CLIENT_PATH_MAX 128 // Maximum path length for a file system client

/**
 * Starts the fs server in a new process
 *
 * @param rd_id resource manager ID of the ramdisk server
 * @param rd_pd_cap PD resource of the ramdisk server
 * @param fs_pd_cap returns the PD resource of the new fs server
 * @param file_manager_id returns the ID of the new file manager
 * @param path_manager_id returns the ID of the new path manager
 * @return 0 on success, or -1 otherwise
 */
int start_xv6fs_pd(uint64_t rd_id,
                   seL4_CPtr rd_pd_cap,
                   seL4_CPtr *fs_pd_cap,
                   uint64_t *file_manager_id,
                   uint64_t *path_manager_id);

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
int xv6fs_client_set_namespace(uint64_t ns_id);

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
    seL4_CPtr file_ep;
    seL4_CPtr filepath_ep;
    seL4_CPtr mo_ep;
    ads_client_context_t *ads_conn;
    pd_client_context_t *pd_conn;

    // Temporary fields for naive implementation
    mo_client_context_t *shared_mem;
    void *shared_mem_vaddr;
} global_xv6fs_client_context_t;
