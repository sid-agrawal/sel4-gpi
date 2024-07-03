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

#include <sel4gpi/resource_server_remote_utils.h>
#include <sel4gpi/resource_space_clientapi.h>

#include <ramdisk_client.h>
#include <fs_shared.h>

#define XV6FS_S "xv6fs Server: "
#define XV6FS_SERVER_DEFAULT_PRIORITY (seL4_MaxPrio - 100)

struct _ads_client_context;
typedef struct _ads_client_context ads_client_context_t;

struct _pd_client_context;
typedef struct _pd_client_context pd_client_context_t;

// Registry of open files
typedef struct _file_registry_entry
{
    resource_server_registry_node_t gen;
    struct file *file; // handle of the open file
} file_registry_entry_t;

// Registry of namespaces
typedef struct _fs_namespace_entry
{
    resource_server_registry_node_t gen;
    resspc_client_context_t res_space_conn;
    char ns_prefix[MAXPATH]; // prefix of this namespace in the default ns
} fs_namespace_entry_t;

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
    resource_server_registry_t file_registry;
    resource_server_registry_t ns_registry;

    // Fields for naive block implementation
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
void xv6fs_request_handler(void *msg_p,
                           void *msg_reply_p,
                           seL4_Word sender_badge,
                           seL4_CPtr cap,
                           bool *need_new_recv_cap);

/**
 * To handle root task requests to the ramdisk server
 */
int xv6fs_work_handler(PdWorkReturnMessage *work);

xv6fs_server_context_t *get_xv6fs_server(void);